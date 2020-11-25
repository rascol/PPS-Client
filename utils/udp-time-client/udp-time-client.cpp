/**
 * @file udp-time-client.cpp
 * @brief client process to connect to  the time service via port 37 using UDP.

 * The client process uses the time message received to check
 * (and optionally to set) the time of the local clock.  the comparison
 * assumes that the local clock keeps time in seconds from 1/1/70
 * which is the UNIX standard, and it assumes that the received time
 * is in seconds since 1900.0 to conform to rfc868.  It computes the
 * time difference by subtracting 2208988800 from the received time
 * to convert it to seconds since 1970 and then makes the comparison
 * directly. If the local machine keeps time in some other way, then
 * the comparison method will have to change, but the rest should be
 * okay.
 *
 * This software was developed with US Government support
 * and it may not be sold, restricted or licensed.  You
 * may duplicate this program provided that this notice
 * remains in all of the copies, and you may give it to
 * others provided they understand and agree to this
 * condition.
 *
 * This program and the time protocol it uses are under
 * development and the implementation may change without
 * notice.
 *
 * For questions or additional information, contact:
 *
 * Judah Levine
 * Time and Frequency Division
 * NIST/847
 * 325 Broadway
 * Boulder, Colorado 80305
 * (303) 492 7785
 * jlevine@boulder.nist.gov
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <ctype.h>

/*
        the following is a list of the servers operated by
        NIST. All of them will support the daytime protocol
        in the format that this program expects.

        Each server may be specified either by name, as in
        time.nist.gov or by ip as in 192.43.244.18. If the
        first character of the specification is a digit, then
        the numerical format is assumed. If a name is entered,
        it is converted to the corresponding ip address using
        the standard DNS query. The program will fail if the
        DNS query cannot find the ip number of the server

        For more information about these servers, look at the
        entry for the Internet Time Service on the Time and
        Frequency Division homepage at
        https://www.nist.gov/pml/time-and-frequency-division
*/
#define NUMSRV 16

const char *serv_ip[NUMSRV]= {
		"time-a-wwv.nist.gov"   ,		// OK
        "utcnist.colorado.edu" ,			// OK
        "time-b-wwv.nist.gov" ,			// OK
        "time-c-wwv.nist.gov"  ,			// OK
        "time-a.nist.gov"    ,			// OK
        "time-b.nist.gov"    ,			// OK
        "time-a.timefreq.bldrdoc.gov"  ,	// OK
        "time-b.timefreq.bldrdoc.gov"  ,	// OK
        "time-c.timefreq.bldrdoc.gov"  ,	// OK
        "time.nist.gov"  ,				// OK
        "time-d-wwv.nist.gov"   ,		// OK
		"utcnist.colorado.edu",			// OK
		"time-a-b.nist.gov",				// OK
		"time-b-b.nist.gov",				// OK
		"time-c-b.nist.gov",				// OK
		"time-d-b.nist.gov"				// OK
};

int sw(int argc, char *argv[], char *let, long int *val)
{
/*
	this subroutine parses switches on the command line.
	switches are of the form -<letter><value>.  If one is
	found, a pointer to the letter is returned in let
	and a pointer to the value in val as a long integer.


	parameters argc and argv are passed in from the main
	calling program.
	Note that if argc = 1 then only the command is left and
	the line is empty.
	if a switch is decoded, the value of the function is 1
	otherwise it is zero.

	a number following the letter is decoded as
	a decimal value unless it has a leading x in which case
	it is decoded as hexadecimal.

	This software was developed with US Government support
	and it may not be sold, restricted or licensed.  You
	may duplicate this program provided that this notice
	remains in all of the copies, and you may give it to
	others provided they understand and agree to this
	condition.

	For questions or additional information, contact:

	Judah Levine
	Time and Frequency Division
	NIST/847
	325 Broadway
	Boulder, Colorado 80303
	(303) 492 7785
	jlevine@time_a.timefreq.bldrdoc.gov
*/

/*
	Either nothing is left or what is left is not a switch.
*/
	if(argc == 1 || *argv[1] != '-')
	{
		*let = '\0';
		*val = 0;
		return(0);
	}
	*let = *++argv[1];   			/* Get the letter after the - character */

	if (*++argv[1] != 'x'){   	    /* If next char is not x, decode number */
		*val = atol(argv[1]);
	}
	else
	{
		argv[1]++;    				/* skip over x and decode value in hex */
		sscanf(argv[1], " %lx ", val);
	}
	return(1);
}

int main (int argc, char *argv[])
{
	long int address;					/* holds ip address */
	int pserv = 37;						/* time port number on server */
	const char *cp = NULL;				/* server address in . notation */
	char *sp;							/* temporary for server address*/
	int aindots[4];          			/* numerical host address in dot notation*/
	char addrbuf[20];        			/* address formatted into dot notation*/
	int j;
	struct sockaddr_in sin;				/* socket address structure */
	int s;								/* socket number */
	int length;							/* size of message */
	char buf[10];						/* holds message */
	unsigned long recdtime;				/* received time in local byte order */
	unsigned long dorg = 2208988800ul;	/* seconds from 1900.0 -> 1970.0*/
	unsigned long netcons;				/* received time in network order */
	struct timeval tvv;					/* holds local time */
	long int diff;						/* time difference, local - NIST */
//	char cc;
//	int stat;
	int use_serv = 12;					/* use server number 13 by default, see below */
	char let;							/* switch letter */
	long int val;						/* value associated with switch*/
	struct hostent *serv0;   			/* pointer to structure returned by gethost */
/*
	parse command line switch to select server

       -u<j>            Use server number j, where j is the index number
                        of the NIST server chosen from the serv_ip list.
                        The first server on the list is number 1, the
                        second is number 2, etc.
                        The default is to use the first server, which
                        is number 1.
                        Note that the array uses C indexing, so that the
                        first entry in the array is number 0, etc.
                        Thus any user response is decremented before
                        being used.

        -u0             The name of the server is given as the next
                        parameter on the command line. The name can
                        be either a name or an ip address in dot
                        notation. The entry will be taken as a name
                        if the first non-blank character is not a
                        digit.
*/
       while( sw(argc, argv, &let, &val) != 0) 	/* switch is present	*/
	   {
		   switch(let)
		   {
			  case 'u':
			   if(val == 0)    					/* Next parameter specifies server */
			   {
				   argc--;
				   argv++;      					/* Skip over the switch */
				   if(argc <= 1)
				   {
						fprintf(stderr, "Expected server name is missing.\n");
						exit(1);
				   }
				   cp = argv[1];  				/* Save the next parameter as the server name */
				   use_serv = 999;    			/* Set flag */
				   break;
			   }
												/* Server 1 has internal index 0 */
			   use_serv = val - 1;
												/* Check if entry is out of range */
			   if(use_serv < 0) use_serv = 0;
			   if(use_serv >= NUMSRV) use_serv = NUMSRV - 1;

			   break;

			  default:
				fprintf(stderr,"Switch %c not recognized.\n", let);
				break;
		   }
		   argc--;      							/* Decrement argument counter */
		   argv++;      							/* and increment pointer */
	  }
/*
	Internet address of selected server
*/
	if (use_serv != 999){
		cp = serv_ip[use_serv];
	}
	if (!isdigit(*cp))       			/* First char not a digit, must convert name */
	{
	   if ((serv0 = gethostbyname(cp)) == NULL)
	   {
			fprintf(stderr, "Cannot resolve name %s\n",cp);
			exit(1);
	   }
	   if (serv0->h_length != 4)
	   {
		  fprintf(stderr, "Length of host address (= %d) is wrong, 4 expected.\n", serv0->h_length);
		  exit(1);
	   }
	   sp = serv0->h_addr_list[0];
	   for (j = 0; j < 4; j++)   		/* Store and convert address */
	   {
		  aindots[j] = *(sp++);
		  aindots[j] &= 0xff;       		/* Turn off sign extension */
	   }
	   sprintf(addrbuf,"%d.%d.%d.%d",
			aindots[0], aindots[1], aindots[2], aindots[3]);
	   cp = addrbuf;
	}

//	printf("\n Using server at address %s", cp);

/*
	Convert address to internal format
*/
	if ((address = inet_addr(cp)) == -1)
	{
	   fprintf(stderr,"Internet address error.\n");
	   exit(1);
	}
	bzero((char *)&sin, sizeof(sin));
	sin.sin_addr.s_addr = address;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(pserv);
/*
	Create the socket and then connect to the server.
	Note that this is a UDP datagram socket.
*/
	if ((s = socket(AF_INET, SOCK_DGRAM, 0) ) < 0)
	{
	   fprintf(stderr,"Socket creation error.\n");
	   exit(1);
	}
	if (connect(s, (struct sockaddr *) &sin, sizeof(sin) ) < 0)
	{
	   perror(" time server Connect failed --");
	   exit(1);
	}
/*
	Send a UDP datagram to start the server going.
*/
	int r = write(s, buf, 10);
	if (r == -1){
		fprintf(stderr, "Write to server failed.\n");
		exit(1);
	}
	length = read(s, &netcons, 4);
	gettimeofday(&tvv, 0);					/* get time as soon as read completes */
	if (length <= 0)
	{
	   fprintf(stderr, "No response from server.\n");
	   close(s);
	   exit(1);
	}
	recdtime = ntohl(netcons);				/* convert to local byte order */
	recdtime -= dorg;						/* convert to seconds since 1970 */
	close(s);
/*
	Now compute difference between local time and received time.
*/
	diff = tvv.tv_sec - recdtime;

	if (tvv.tv_usec >= 500000l) diff++;		/* round microseconds */

//	printf("\nLocal Clock - NIST = %ld second(s).", diff);

	printf("%ld\n", diff);

//	if (diff == 0)
//	{
//	   printf("\n No adjustment is needed.\n");
//	   exit(0);
//	}

//	printf("\n Do you want to adjust the local clock ? [y/n] ");
//	cc = getchar();
//	if ((cc == 'y') || (cc == 'Y'))
//	   {
//	   if(diff > 2100l)
//	   {
//	      printf("\n Adjustment limited to -2100 s.");
//	      diff = 2100l;
//	   }
//	   if (diff < -2100l)
//	   {
//	      printf("\n Adjustment limited to +2100 s.");
//	     diff= -2100l;
//	   }
//	   tvv.tv_sec = -diff;
//	   tvv.tv_usec = 0;
//	   stat = adjtime(&tvv, &tgg);
//	   if (stat == 0) printf("\n Adjustment was performed.\n");
//	   else perror("Adjustment failed. ");
//	}
	exit(0);
}

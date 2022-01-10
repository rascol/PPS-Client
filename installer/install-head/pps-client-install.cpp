/*
 * pps-client-install.cpp
 *
 * Copyright (C) 2016-2020  Raymond S. Connell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <errno.h>

const char *version = "pps-client-installer v2.0.4";
const char *cfgVersion = "2.0.4";

char *configbuf = NULL;
unsigned char *fbuf = NULL;
const char *config_file = "./pps-client.conf";

#define CONFIG_FILE_SZ 10000
#define MAX_CONFIGS 32
/**
 * These defines correspond to
 * the valid_config[] array below.
 */
#define EXECDIR 1
#define SERVICEDIR 2
#define CONFIGDIR 4
#define DOCDIR 8

/**
 * Recognized configuration strings for the PPS-Client
 * configuration file.
 */
const char *valid_config[] = {
		"execdir",
		"servicedir",
		"configdir",
		"docdir"
};

char configBuf[CONFIG_FILE_SZ];
char *configVals[MAX_CONFIGS];
unsigned int config_select = 0;

char execdir[100];
char servicedir[100];
char configdir[100];
char docdir[100];

bool isDefaultLine(char *line){
	if (line[0] == '\n' 					// Empty line
			|| line[0] == '#'){			// Commented-out line
		return true;
	}
	return false;
}

bool allOptsAreCommentedOut(char *configbuf){

	char *line = configbuf;

	while (line[0] != '\0'){
		if (isDefaultLine(line)){
			line = strstr(line, "\n\0");
			if (line[0] == '\n'){
				line += 1;
			}
		}
		else if (line[0] != '\0'){
			return false;
		}
	}
	return true;
}

/**
 * Reads the PPS-Client config file and sets bits
 * in config_select to 1 or 0 corresponding to
 * whether a particular configVals appears in the
 * config file. The configVals from the file is then
 * copied to configBuf and a pointer to that string is
 * placed in the configVals array.
 *
 * If the configVals did not occur in the config file
 * then configVals has a NULL char* in the corresponding
 * location.
 *
 * @returns 0 on success, else -1 on error.
 */
int readConfigFile(void){

	struct stat stat_buf;
	struct stat configFileStat;


	int rvs = stat(config_file, &configFileStat);
	if (rvs == -1){
		fprintf(stderr, "readConfigFile(): Config file not found.\n");
		return -1;
	}

	int fd = open(config_file, O_RDONLY);
	if (fd == -1){
		fprintf(stderr, "Unable to open %s", config_file);
		return -1;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	if (sz >= CONFIG_FILE_SZ){
		fprintf(stderr, "readConfigFile(): not enough space allocated for config file.\n");
		return -1;
	}

	int rv = read(fd, configBuf, sz);
	if (rv == -1 || sz != rv){
		fprintf(stderr, "Unable to read configBuf");
		return -1;
	}
	close(fd);

	configBuf[sz] = '\0';

	int nCfgStrs = 0;
	int i;

	char *pToken = strtok(configBuf, "\n");				// Separate tokens at "\n".

	while (pToken != NULL){
		if (strlen(pToken) != 0){						// If not a blank line.
			for (int j = 0; j < 10; j++){				// Skip leading spaces.
				if (pToken[0] == ' '){
					pToken += 1;
				}
				else {
					break;								// Break on first non-space character.
				}
			}

			if (pToken[0] != '#'){						// Ignore comment lines.
				configVals[nCfgStrs] = pToken;
				nCfgStrs += 1;
			}
		}
		pToken = strtok(NULL, "\n");					// Get the next token.
	}

	if (nCfgStrs == 0){
		return 0;
	}

	for (i = 0; i < nCfgStrs; i++){						// Compact configBuf to remove string terminators inserted by
		if (i == 0){									// strtok() so that configBuf can be searched as a single string.
			strcpy(configBuf, configVals[i]);
		}
		else {
			strcat(configBuf, configVals[i]);
		}
		strcat(configBuf, "\n");
	}

	int nValidCnfgs = sizeof(valid_config) / sizeof(char *);

	char **configVal = configVals;						// Use configVals to return pointers to value strings
	char *value;

	for (i = 0; i < nValidCnfgs; i++){

		char *found = strstr(configBuf, valid_config[i]);
		if (found != NULL){
			config_select |= 1 << i;					// Set a bit in config_select

			value = strpbrk(found, "=");				// Get the value string following '='.
			value += 1;

			configVal[i] = value;						// Point to configVals[i] value string in configBuf
		}
		else {
			config_select &= ~(1 << i);					// Clear a bit in config_select
			configVal[i] = NULL;
		}
	}

	for (i = 0; i < nValidCnfgs; i++){					// Replace the "\n" at the end of each value
		if (configVal[i] != NULL){						// string with a string terminator.
			value = strpbrk(configVal[i], "\n");
			if (value != NULL){
				*value = 0;
			}
		}
	}

	return 0;
}



/**
 * Retrieves the string from the config file assigned
 * to the valid_config string with value key.
 *
 * @param[in] key The config key corresponding to a string in
 * the valid_config[] array above.
 *
 * @returns The string assigned to the key.
 */
char *getString(int key){
	char *str;
	int len;
	int i = round(log2(key));

	if (config_select & key){

		str = configVals[i];

		len = strlen(str);

		while (str[len-1] == ' '){
			str[len-1] = '\0';
			len -= 1;
		}

		return str;
	}
	return NULL;
}

/**
 * Processes the files and configuration settings specified
 * by the PPS-Client config file.
 */
int processConfig(void){

	int rv = readConfigFile();
	if (rv == -1){
		exit(1);
	}

	struct stat dirStat;

	char *sp = getString(CONFIGDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for configdir. %s: %s\n", strerror(errno), sp);
			exit(1);
		}

		strcpy(configdir, sp);
	}

	sp = getString(EXECDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for execdir. %s: %s\n", strerror(errno), sp);
			exit(1);
		}

		strcpy(execdir, sp);
	}

	sp = getString(SERVICEDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for servicedir. %s: %s\n", strerror(errno), sp);
			exit(1);
		}

		strcpy(servicedir, sp);
	}

	sp = getString(DOCDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for docdir. %s: %s\n", strerror(errno), sp);
			exit(1);
		}

		strcpy(docdir, sp);
	}

	return 0;
}

int sysCommand(const char *cmd){
	int rv = system(cmd);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("system command failed: %s\n", cmd);

		if (configbuf!= NULL){
			delete configbuf;
		}
		if (fbuf != NULL){
			delete fbuf;
		}
		exit(EXIT_FAILURE);
	}
	return 0;
}

int doSysCommand(const char *arg1, char *arg2, const char *arg3){
	char cmdStr[200];

	strcpy(cmdStr, arg1);
	strcat(cmdStr, arg2);
	strcat(cmdStr, arg3);

	int rv = system(cmdStr);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("system command failed: %s\n", cmdStr);

		if (configbuf!= NULL){
			delete configbuf;
		}
		if (fbuf != NULL){
			delete fbuf;
		}
		exit(EXIT_FAILURE);
	}
	return 0;
}

void movefiles(void){
	char cmdStr[200];

	sysCommand("tar xzvf pkg.tar.gz");

	printf("Moving pps-client to %s/pps-client\n", execdir);
	doSysCommand("mv ./pkg/pps-client ", execdir, "/pps-client");

	printf("Moving pps-client.service to %s/pps-client.service\n", servicedir);
	doSysCommand("mv ./pkg/pps-client.service ", servicedir, "/pps-client.service");
	doSysCommand("chmod 664 ", servicedir, "/pps-client.service");
	sysCommand("systemctl daemon-reload");

	strcpy(cmdStr, configdir);
	strcat(cmdStr, "/pps-client.conf");
	int fdc = open(cmdStr, O_RDONLY);
	if (fdc == -1){													// No config file.
		printf("Moving pps-client.conf to %s/pps-client.conf\n", configdir);

		doSysCommand("mv ./pkg/pps-client.conf ", configdir, "/pps-client.conf");
	}
	else {															// Config file exists.
		int configbufLen = CONFIG_FILE_SZ;
		configbuf = new char[configbufLen];
		memset(configbuf, 0, configbufLen);

		int rv = read(fdc, configbuf, configbufLen-1);
		if (rv == -1){
			printf("Read config file failed. Exiting.\n");
			exit(EXIT_FAILURE);
		}

		if (allOptsAreCommentedOut(configbuf)){
			printf("Moving pps-client.conf to %s/pps-client.conf\n", configdir);
			doSysCommand("mv ./pkg/pps-client.conf ", configdir, "/pps-client.conf");
		}
		else {
			printf("Modified file, %s/pps-client.conf, was not replaced. Instead, config was written to %s/pps-client.conf.default.\n", configdir, configdir);
			doSysCommand("mv ./pkg/pps-client.conf ", configdir, "/pps-client.conf.default");
		}

		delete configbuf;
		configbuf = NULL;

		close(fdc);
	}

	printf("Moving pps-client-remove to %s/pps-client-remove\n", execdir);
	doSysCommand("mv ./pkg/pps-client-remove ", execdir, "/pps-client-remove");

	printf("Moving pps-client-stop to %s/pps-client-stop\n", execdir);
	doSysCommand("mv ./pkg/pps-client-stop ", execdir, "/pps-client-stop");
	doSysCommand("chmod +x ", execdir, "/pps-client-stop");

	printf("Moving normal-params to %s/normal-params\n", execdir);
	doSysCommand("mv ./pkg/normal-params ", execdir, "/normal-params");
	doSysCommand("chmod +x ", execdir, "/normal-params");

	printf("Moving udp-time-client to %s/udp-time-client\n", execdir);
	doSysCommand("mv ./pkg/udp-time-client ", execdir, "/udp-time-client");
	doSysCommand("chmod +x ", execdir, "/udp-time-client");

	printf("Moving README.md to %s/pps-client/README.md\n", docdir);
	doSysCommand("mkdir ", docdir, "/pps-client");
	doSysCommand("mv ./pkg/README.md ", docdir, "/pps-client/README.md");

	doSysCommand("mkdir ", docdir, "/pps-client/figures");
	doSysCommand("mv ./pkg/frequency-vars.png ", docdir, "/pps-client/figures/frequency-vars.png");
	doSysCommand("mv ./pkg/offset-distrib.png ", docdir, "/pps-client/figures/offset-distrib.png");
	doSysCommand("mv ./pkg/StatusPrintoutOnStart.png ", docdir, "/pps-client/figures/StatusPrintoutOnStart.png");
	doSysCommand("mv ./pkg/StatusPrintoutAt10Min.png ", docdir, "/pps-client/figures/StatusPrintoutAt10Min.png");
	doSysCommand("mv ./pkg/RPi_with_GPS.jpg ", docdir, "/pps-client/figures/RPi_with_GPS.jpg");
	doSysCommand("mv ./pkg/InterruptTimerDistrib.png ", docdir, "/pps-client/figures/InterruptTimerDistrib.png");
	sysCommand("mv ./pkg/time.png /usr/share/doc/pps-client/figures/time.png");

	printf("Moving Doxyfile to %s/pps-client/Doxyfile\n", docdir);
	doSysCommand("mv ./pkg/Doxyfile ", docdir, "/pps-client/Doxyfile");

	printf("Moving pps-client.md to %s/pps-client/client/pps-client.md\n", docdir);
	doSysCommand("mkdir ", docdir, "/pps-client/client");
	doSysCommand("mv ./pkg/client/pps-client.md ", docdir, "/pps-client/client/pps-client.md");

	doSysCommand("mkdir ", docdir, "/pps-client/client/figures");
	doSysCommand("mv ./pkg/client/figures/jitter-spike.png ", docdir, "/pps-client/client/figures/jitter-spike.png");
	doSysCommand("mv ./pkg/client/figures/pps-offsets-stress.png ", docdir, "/pps-client/client/figures/pps-offsets-stress.png");
	doSysCommand("mv ./pkg/client/figures/pps-offsets-to-300.png ", docdir, "/pps-client/client/figures/pps-offsets-to-300.png");
	doSysCommand("mv ./pkg/client/figures/pps-offsets-to-720.png ", docdir, "/pps-client/client/figures/pps-offsets-to-720.png");
	doSysCommand("mv ./pkg/client/figures/StatusPrintoutAt10Min.png ", docdir, "/pps-client/client/figures/StatusPrintoutAt10Min.png");
	doSysCommand("mv ./pkg/client/figures/StatusPrintoutOnStart.png ", docdir, "/pps-client/client/figures/StatusPrintoutOnStart.png");
	doSysCommand("mv ./pkg/client/figures/pps-jitter-distrib-RPi3.png ", docdir, "/pps-client/client/figures/pps-jitter-distrib-RPi3.png");
	sysCommand("rm -rf ./pkg");

	sysCommand("rm pkg.tar.gz");

	return;
}

int main(int argc, char *argv[]){
	unsigned char pkg_start[8];
	pkg_start[0] = 0xff;
	pkg_start[1] = 0x00;
	pkg_start[2] = 0xff;
	pkg_start[3] = 0x00;
	pkg_start[4] = 0xff;
	pkg_start[5] = 0x00;
	pkg_start[6] = 0xff;
	pkg_start[7] = 0x00;

	uid_t uid = geteuid();
	if (uid != 0){
		printf("Requires superuser privileges. Please sudo this command.\n");
		exit(EXIT_FAILURE);
	}

	processConfig();

	unsigned char *ps = pkg_start;

	struct stat stat_buf;

	int fd = open(argv[0], O_RDONLY);					// Prepare to read this program as a binary file
	if (fd == -1){
		printf("Program binary %s was not found\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;							// Program size including tar file attachment

	fbuf = new unsigned char[sz];						// A buffer to hold the binary file

	ssize_t rd = read(fd, fbuf, sz);					// Read the PPS-Client program binary into the buffer
	close(fd);

	if (rd == -1){
		printf("Error reading %s\n", argv[0]);
		delete fbuf;
		exit(EXIT_FAILURE);
	}

	unsigned char *ptar = fbuf;
	int i;
	for (i = 0; i < (int)rd; i++, ptar += 1){			// Get a pointer to the tar file separator

		if (ptar[0] == ps[0] && ptar[1] == ps[1] && ptar[2] == ps[2] && ptar[3] == ps[3]
				&& ptar[4] == ps[4] && ptar[5] == ps[5] && ptar[6] == ps[6] && ptar[7] == ps[7]){
			ptar += 8;									// ptar now points to the tar file
			break;
		}
	}

	if (i == rd){
		printf("pkg_start code was not found.\n");
		delete fbuf;
		exit(EXIT_FAILURE);
	}
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
	sz -= i + 8;											// Set to the size of the tar file
															// Create the tar archive
	int fd2 = open("pkg.tar.gz", O_CREAT | O_WRONLY, mode);
	if (fd2 == -1){
		printf("Unable to create the tar file\n");
		delete fbuf;
		exit(EXIT_FAILURE);
	}

	int wrt = (int)write(fd2, ptar, sz);					// Write to the tar file from ptar

	delete fbuf;											// test
	fbuf = NULL;

	if (wrt == -1){
		close(fd2);
		printf("Error writing tar file.\n");
		exit(EXIT_FAILURE);
	}

	if ((int)wrt != sz){
		close(fd2);
		printf("Incomplete write to tar file. sz: %d wrt: %d\n", sz, wrt);
		exit(EXIT_FAILURE);
	}

	close(fd2);

	movefiles();

	printf("Done.\n");

	return 0;
}



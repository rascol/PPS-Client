/*
 * pps-client-remove.cpp
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
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>

#define CONFIG_FILE_SZ 10000
#define MAX_CONFIGS 32

const char *version = "pps-client-remove v2.0.0";

/**
 * These defines correspond to
 * the valid_config[] array below.
 */
#define EXECDIR 1
#define SERVICEDIR 2
#define CONFIGDIR 4
#define DOCDIR 8
#define LOGDIR 16

char configBuf[CONFIG_FILE_SZ];
char *configVals[MAX_CONFIGS];
unsigned int config_select = 0;

/**
 * Recognized configuration strings for the PPS-Client
 * configuration file.
 */
const char *valid_config[] = {
		"execdir",
		"servicedir",
		"configdir",
		"docdir",
		"logdir"
};

char execdir[100];
char servicedir[100];
char configdir[100];
char docdir[100];
char logdir[100];

const char *config_file = "/XXXX/pps-client.conf";

/**
 * Reads the PPS-Client config file and sets bits
 * in config_select to 1 or 0 corresponding to
 * whether a particular configVals appears in the
 * config file.
 *
 * The configVals from the file is then
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

	sp = getString(LOGDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for logdir. %s: %s\n", strerror(errno), sp);
			exit(1);
		}

		strcpy(logdir, sp);
	}

	return 0;
}

int sysCommand(const char *cmd){
	int rv = system(cmd);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("system command failed: %s\n", cmd);
		return -1;
	}
	return 0;
}

int doSysCommand(const char *arg1, char *arg2, const char *arg3){
	char cmdStr[120];

	strcpy(cmdStr, arg1);
	strcat(cmdStr, arg2);
	strcat(cmdStr, arg3);

	int rv = system(cmdStr);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("system command failed: %s\n", cmdStr);
		exit(EXIT_FAILURE);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	uid_t uid = geteuid();
	if (uid != 0){
		printf("Requires superuser privileges. Please sudo this command.\n");
		return 1;
	}

	processConfig();

	sysCommand("systemctl stop pps-client");
	sysCommand("pps-client-stop");					// In case not started as a service

	if (argc > 1 && strcmp(argv[1], "-a") == 0){
		printf("Removing %s/pps-client.conf\n", configdir);
		doSysCommand("rm -f ", configdir, "/pps-client.conf");
	}

	printf("Removing %s/pps-client\n", execdir);
	doSysCommand("rm -f ", execdir, "/pps-client");

	printf("Removing %s/pps-client-stop\n", execdir);
	doSysCommand("rm -f ", execdir, "pps-client-stop");

	printf("Removing %s/pps-client.service\n", servicedir);
	doSysCommand("rm -f ", servicedir, "/pps-client.service");

	printf("Removing %s/pps-client.log\n", logdir);
	doSysCommand("rm -f ", logdir, "/pps-client.log");

	printf("Removing %s/pps-client directory\n", docdir);
	doSysCommand("rm -rf ", docdir, "/pps-client");

	printf("Removing %s/udp-time-client\n", execdir);
	doSysCommand("rm -f ", execdir, "/udp-time-client");

	printf("Removing %s/normal-params\n", execdir);
	doSysCommand("rm -f ", execdir, "/normal-params");

	printf("Removing %s/pps-client-remove\n", execdir);
	strcat(execdir, "/");
	doSysCommand("rm -f ", execdir, argv[0]);


	return 0;
}

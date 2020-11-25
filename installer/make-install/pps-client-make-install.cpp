/*
 * pps-client-make-install.cpp
 *
 * Copyright (C) 2016 - 2020 Raymond S. Connell
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
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

const char *version = "pps-client-installer v2.0.0";

int main(int argc, char *argv[]){
	unsigned char archive_start[8];
	archive_start[0] = 0xff;
	archive_start[1] = 0x00;
	archive_start[2] = 0xff;
	archive_start[3] = 0x00;
	archive_start[4] = 0xff;
	archive_start[5] = 0x00;
	archive_start[6] = 0xff;
	archive_start[7] = 0x00;

	struct stat stat_buf;

	char *vers = argv[1];						// Get the kernel version
	char installName[50];
	strcpy(installName, "pps-client-");
	strcat(installName, vers);					// Form filename pps-client-x.y.z

	int fd_hd = open("./installer/pps-client-install-hd", O_RDONLY);	// Open the install head
	if (fd_hd == -1){
		printf("File not found: ./installer/pps-client-install-hd\n");
		return 1;
	}

	fstat(fd_hd, &stat_buf);
	int sz_hd = stat_buf.st_size;				// Get the install head size

	int fd_tar = open("pkg.tar.gz", O_RDONLY);	// Prepare to read the tar as a binary file
	if (fd_tar == -1){
		printf("File not found: pkg.tar.gz\n");
		close(fd_hd);
		return 1;
	}

	fstat(fd_tar, &stat_buf);
	int sz_tar = stat_buf.st_size;

	int sz_fbuf = sz_hd + sz_tar + 8;
	unsigned char *fbuf = new unsigned char[sz_fbuf];

	ssize_t rd_hd = read(fd_hd, fbuf, sz_hd);
	close(fd_hd);

	if (rd_hd != sz_hd){
		printf("Error reading ./installer/pps-client-install-hd\n");
		close(fd_tar);
		delete fbuf;
		return 1;
	}

	unsigned char *pbuf = fbuf + sz_hd;

	for (int i = 0; i < 8; i++, pbuf += 1){
		*pbuf = archive_start[i];
	}

	ssize_t rd_tar = read(fd_tar, pbuf, sz_tar);
	close(fd_tar);

	if (rd_tar != sz_tar){
		printf("Error reading pkg.tar\n");
		delete fbuf;
		return 1;
	}

	remove(installName);

	int fd_ins = open(installName, O_CREAT | O_WRONLY, NULL);
	if (fd_ins == -1){
		printf("Unable to create install file\n");
		delete fbuf;
		return 1;
	}

	ssize_t wrt_ins = write(fd_ins, fbuf, sz_fbuf);

	if (wrt_ins != sz_fbuf){
		printf("Error writing install file.\n");
		close(fd_ins);
		delete fbuf;
		return 1;
	}

	fchmod(fd_ins, S_IRWXU | S_IRWXG);

	close(fd_ins);
	delete fbuf;
	return 0;
}

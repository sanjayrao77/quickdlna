/*
 * misc.c - some handy utility functions
 * Copyright (C) 2024 Sanjay Rao
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#if 0
#ifdef DEBUG
#undef DEBUG
#endif
#endif
#include "common/conventions.h"

#include "misc.h"

unsigned int slowtou(char *str) {
unsigned int ret=0;
switch (*str) {
	case '1': ret=1; break;
	case '2': ret=2; break;
	case '3': ret=3; break;
	case '4': ret=4; break;
	case '5': ret=5; break;
	case '6': ret=6; break;
	case '7': ret=7; break;
	case '8': ret=8; break;
	case '9': ret=9; break;
	case '+':
	case '0': break;
	default: return 0; break;
}
while (1) {
	str++;
	switch (*str) {
		case '9': ret=ret*10+9; break;
		case '8': ret=ret*10+8; break;
		case '7': ret=ret*10+7; break;
		case '6': ret=ret*10+6; break;
		case '5': ret=ret*10+5; break;
		case '4': ret=ret*10+4; break;
		case '3': ret=ret*10+3; break;
		case '2': ret=ret*10+2; break;
		case '1': ret=ret*10+1; break;
		case '0': ret=ret*10; break;
		default: return ret; break;
	}
}
return ret;
}

uint64_t slowtou64(char *str) {
uint64_t ret=0;
switch (*str) {
	case '1': ret=1; break;
	case '2': ret=2; break;
	case '3': ret=3; break;
	case '4': ret=4; break;
	case '5': ret=5; break;
	case '6': ret=6; break;
	case '7': ret=7; break;
	case '8': ret=8; break;
	case '9': ret=9; break;
	case '+':
	case '0': break;
	default: return 0; break;
}
while (1) {
	str++;
	switch (*str) {
		case '9': ret=ret*10+9; break;
		case '8': ret=ret*10+8; break;
		case '7': ret=ret*10+7; break;
		case '6': ret=ret*10+6; break;
		case '5': ret=ret*10+5; break;
		case '4': ret=ret*10+4; break;
		case '3': ret=ret*10+3; break;
		case '2': ret=ret*10+2; break;
		case '1': ret=ret*10+1; break;
		case '0': ret=ret*10; break;
		default: return ret; break;
	}
}
return ret;
}

#if 0
int parseipv4_utils(uint32_t *ipv4_out, char *str) {
int partcount=0;
unsigned int part=0;
uint32_t rev32=0; // doesn't need to be zeroed

while (1) {
	int v;
	v=*str;
	switch (v) {
		case 0: goto doublebreak; break;
		case '.':
			rev32=rev32<<8;
			rev32|=(unsigned char)part;
			partcount++;
			if (partcount==4) GOTOERROR;
			part=0;
			break;
		case '0': part=part*10; break;
		case '1': part=part*10+1; break;
		case '2': part=part*10+2; break;
		case '3': part=part*10+3; break;
		case '4': part=part*10+4; break;
		case '5': part=part*10+5; break;
		case '6': part=part*10+6; break;
		case '7': part=part*10+7; break;
		case '8': part=part*10+8; break;
		case '9': part=part*10+9; break;
		default: GOTOERROR;
	}

	str++;
}
doublebreak:
if (partcount!=3) GOTOERROR;

uint32_t ipv4;
ipv4=rev32&0xff;
ipv4=ipv4<<8; rev32=rev32>>8; ipv4|=rev32&0xff;
ipv4=ipv4<<8; rev32=rev32>>8; ipv4|=rev32&0xff;
ipv4=ipv4<<8; rev32=rev32>>8; ipv4|=rev32&0xff;
*ipv4_out=ipv4;
return 0;
error:
	return -1;
}
#endif

int readn(int fd, unsigned char *msg, unsigned int len) {
while (len) {
	ssize_t k;
	k=read(fd,(char *)msg,len);
	if (k<1) {
		if ((k<0) && (errno==EINTR)) continue;
		return -1;
	}
	len-=k;
	msg+=k;
}
return 0;
}
int writen(int fd, unsigned char *msg, unsigned int len) {
while (len) {
	ssize_t k;
	k=write(fd,(char *)msg,len);
	if (k<1) {
		if ((k<0) && (errno==EINTR)) continue;
		return -1;
	}
	len-=k;
	msg+=k;
}
return 0;
}

int timeout_readn(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires) {
struct pollfd pollfd;
int istimeout=0;

if (!len) return 0;

pollfd.fd=fd;
pollfd.events=POLLIN;
while (1) {
	time_t now;
	int r;

	now=time(NULL);
	if (expires <= now) {
		istimeout=1;
		GOTOERROR;
	}
	r=poll(&pollfd,1,(expires-now)*1000);
	if (r<0) {
		if (errno==EINTR) continue;
		GOTOERROR;
	}
	if (r && (pollfd.revents&POLLIN)) {
		ssize_t k;
		k=read(fd,(char *)msg,len);
		if (k<1) {
			if ((k<0) && (errno==EINTR)) continue;
			GOTOERROR;
		}
		len-=k;
		if (!len) break;
		msg+=k;
	}
}
return 0;
error:
	*istimeout_errorout=istimeout;
	return -1;
}

int timeout_writen(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires) {
struct pollfd pollfd;
int istimeout=0;

if (!len) return 0;

pollfd.fd=fd;
pollfd.events=POLLOUT;
while (1) {
	time_t now;
	int r;

	now=time(NULL);
	if (expires <= now) {
		istimeout=1;
		GOTOERROR;
	}
	r=poll(&pollfd,1,(expires-now)*1000);
	if (r<0) {
		if (errno==EINTR) continue;
		GOTOERROR;
	}
	if (r && (pollfd.revents&POLLOUT)) {
		ssize_t k;
		k=write(fd,(char *)msg,len);
		if (k<1) {
			if ((k<0) && (errno==EINTR)) continue;
			GOTOERROR;
		}
		len-=k;
		if (!len) break;
		msg+=k;
	}
}
return 0;
error:
	*istimeout_errorout=istimeout;
	return -1;
}

int timeout_readpacket(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires) {
struct pollfd pollfd;
int istimeout=0;

if (!len) return 0;

pollfd.fd=fd;
pollfd.events=POLLIN;
while (1) {
	time_t now;
	int r;

	now=time(NULL);
	if (expires <= now) {
		istimeout=1;
		GOTOERROR;
	}
	r=poll(&pollfd,1,(expires-now)*1000);
	if (r<0) {
		if (errno==EINTR) continue;
		GOTOERROR;
	}
	if (r && (pollfd.revents&POLLIN)) {
		ssize_t k;
		k=read(fd,(char *)msg,len);
		if (k<1) {
			if ((k<0) && (errno==EINTR)) continue;
			GOTOERROR;
		}
		return k;
	}
}
error:
	*istimeout_errorout=istimeout;
	return -1;
}

void httpctime_misc(char *dest, time_t t) {
/* converts from "Wed Dec 11 01:37:18 2002" to "Mon, 31 Dec 2002 23:59:59 GMT"
 * dest[30]
*/
char *src;

src=asctime(gmtime(&t));
dest[29]='\0';
memset(dest,' ',29);
memcpy(dest,src,3);
dest[3]=',';
memcpy(dest+5,src+8,2);
memcpy(dest+8,src+4,3);
memcpy(dest+12,src+20,4);
memcpy(dest+17,src+11,8);
memcpy(dest+26,"GMT",3);
}


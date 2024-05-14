/*
 * lineio.c - do buffered line io on tcp sockets
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <ctype.h>
// #define DEBUG
#include "common/conventions.h"
#include "misc.h"

#include "lineio.h"

CLEARFUNC(lineio);

void voidinit_lineio(struct lineio *lineio, unsigned char *buffer, unsigned int bufferlen) {
lineio->buff=buffer;
lineio->bufflen=bufferlen;
}

void reset_lineio(struct lineio *lineio) {
lineio->cursor=lineio->buff;
lineio->towritecount=lineio->bufflen;
lineio->unreadcount=0;
}
static inline unsigned int checkunread(struct lineio *lineio) {
unsigned char *walk;
unsigned int ui;

ui=lineio->unreadcount;
if (!ui) return 0;
walk=lineio->cursor;
while (1) {
	if (*walk=='\n') {
		return (unsigned int)(walk+1-lineio->cursor);
	}
	ui--;
	if (!ui) break;
	walk++;
}
return 0;
}

unsigned char *gets_lineio(int *istimeout_errorout, unsigned int *len_out, struct lineio *lineio, int fd, time_t expires) {
unsigned char *ret,*dest;
unsigned int linelen;

linelen=checkunread(lineio);
if (linelen) {
	ret=lineio->cursor;
	lineio->cursor+=linelen;
	lineio->unreadcount-=linelen;
	*len_out=linelen;
	return ret;
}
dest=lineio->cursor+lineio->unreadcount;
while (1) {
	if (!lineio->towritecount) {
		if (lineio->unreadcount==lineio->bufflen) { // we're full
			GOTOERROR;
		}
		memmove(lineio->buff,lineio->cursor,lineio->unreadcount);
		lineio->towritecount=lineio->bufflen-lineio->unreadcount;
		lineio->cursor=lineio->buff;
		dest=lineio->cursor+lineio->unreadcount;
	}
	{
		int istimeout;
		int k;
		k=timeout_readpacket(&istimeout,fd,dest,lineio->towritecount,expires);
		if (k<=0) {
			if (k<0) GOTOERROR;
			if (istimeout) {
				*istimeout_errorout=1;
				return NULL;
			}
		}
		lineio->towritecount-=k;
		{
			unsigned char *temp;
			unsigned int ui;
			temp=dest;
			for (ui=0;ui<k;ui++) {
				if (*temp=='\n') {
					linelen=lineio->unreadcount+ui+1;
					ret=lineio->cursor;
					lineio->cursor+=linelen;
					lineio->unreadcount+=k-linelen;
					*len_out=linelen;
					return ret;
				}
				temp++;
			}
		}
		lineio->unreadcount+=k;
		dest+=k;
	}
}
error:
	*istimeout_errorout=0;
	return NULL;
}

int getpost_lineio(int *istimeout_errorout, struct lineio *lineio, int fd, time_t expires, unsigned char *dest, unsigned int destlen) {
if (destlen<=lineio->unreadcount) {
	memcpy(dest,lineio->cursor,destlen);
	lineio->cursor+=destlen;
	lineio->unreadcount-=destlen;
	return 0;
}
unsigned int ui;
int istimeouterror;

ui=lineio->unreadcount;
memcpy(dest,lineio->cursor,ui);
dest+=ui;
destlen-=ui;
lineio->cursor+=ui;
lineio->unreadcount=0;
if (timeout_readn(&istimeouterror,fd,dest,destlen,expires)) {
	if (istimeouterror) {
		*istimeout_errorout=1;
		return -1;
	}
	GOTOERROR;
}
return 0;
error:
	*istimeout_errorout=0;
	return -1;
}


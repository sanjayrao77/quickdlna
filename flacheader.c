/*
 * flacheader.c - read flac file header values
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
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <dirent.h>
// #define DEBUG
#include "common/conventions.h"
#include "misc.h"

#include "flacheader.h"

static unsigned int charstouint(unsigned char c1, unsigned char c2,
				unsigned char c3, unsigned char c4) {
return (c1<<24)|(c2<<16)|(c3<<8)|c4;
}
static unsigned int charstouint2(unsigned char *c) {
return (c[0]<<24)|(c[1]<<16)|(c[2]<<8)|c[3];
}

static unsigned int charstouintr(unsigned char *c) {
return (c[3]<<24)|(c[2]<<16)|(c[1]<<8)|c[0];
}

#define SIZE_SIMPLEBUFFER	(1024*2)

struct simplebuffer {
	unsigned int pos;
	unsigned char buffer[SIZE_SIMPLEBUFFER];
};

static void reset_simplebuffer(struct simplebuffer *sb) {
sb->pos=0;
}

static int load_simplebuffer(struct simplebuffer *sb, char *filename) {
int fd=-1;
fd=open(filename,O_RDONLY);
if (fd<0) GOTOERROR;
if (read(fd,sb->buffer,SIZE_SIMPLEBUFFER)!=SIZE_SIMPLEBUFFER) GOTOERROR;
close(fd);
return 0;
error:
	if (fd>=0) close(fd);
	return -1;
}

static int read_simplebuffer(struct simplebuffer *sb, unsigned char *dest, unsigned int len) {
if (sb->pos+len>=SIZE_SIMPLEBUFFER) {
#ifdef DEBUG
	fprintf(stderr,"%s:%d attempted to seek past buffer: %u + %u\n",__FILE__,__LINE__,sb->pos,len);
#endif
	return -1;
}
memcpy(dest,sb->buffer+sb->pos,len);
sb->pos+=len;
return 0;
}

static void seek_simplebuffer(struct simplebuffer *sb, unsigned int len) {
sb->pos+=len;
}

static void reset_flacheader(struct flacheader *p) {
p->title[0]='\0';
p->artist[0]='\0';
p->album[0]='\0';
p->date[0]='\0';
p->duration=0;
p->tracknumber=0;
}

static int readheaderfromfile(struct flacheader *dest, char *filename) {
unsigned char buff4[4];
struct simplebuffer simplebuffer;

reset_simplebuffer(&simplebuffer);

if (load_simplebuffer(&simplebuffer,filename)) GOTOERROR;
if (read_simplebuffer(&simplebuffer,buff4,4)) goto error;
if (memcmp(buff4,"fLaC",4)) goto error;

#if 0
	fprintf(stderr,"Checking flac: %s\n",dest->filename);
#endif

while (1) {
	int islast=0;
	unsigned int len;
	unsigned char blocktype;
	unsigned char buff[512+1];
	if (read_simplebuffer(&simplebuffer,buff4,4)) goto error;
	islast=buff4[0]&128;
	len=charstouint(0,buff4[1],buff4[2],buff4[3]);
	blocktype=buff4[0]&0x7f;
#if 0
	fprintf(stderr,"blocktype: %u, length: %u\n", blocktype,len);
#endif

	if ((blocktype==0)&&(len==34)) {
		unsigned char *temp=(unsigned char *)buff;
		unsigned int ui;
		if (read_simplebuffer(&simplebuffer,temp,34)) goto error;
		ui=charstouint2(temp+10);
		ui=ui>>12;
		dest->samplerate=ui;
		ui=charstouint2(temp+10);
		ui=ui&15;
		dest->high_samplecount=ui;
		ui=charstouint2(temp+14);
		dest->low_samplecount=ui;
	} else if ((blocktype==4)&&(len<512)) {
		unsigned int ui;
		unsigned char *temp=buff;
		unsigned int comments;
		if (read_simplebuffer(&simplebuffer,buff,len)) goto error;
		buff[len]='\0';

		ui=charstouintr(temp);
#if 0
		{
			char *temp2=temp+4;
			fprintf(stderr,"Title %u bytes: ",ui);
			for (;ui;ui--) { fputc(isprint(temp2[0])?temp2[0]:'?',stderr); temp2++; }
			fputc('\n',stderr);
		}
#endif
		temp+=4+ui;
		comments=charstouintr(temp);
		temp+=4;
		while (comments) {
			char pre0;
			char *tag;
			ui=charstouintr(temp);
			temp+=4;
			pre0=temp[ui];
			temp[ui]='\0';
			tag=(char *)temp;
			if (!strncasecmp(tag,"TRACKNUMBER=",12)) {
				dest->tracknumber=slowtou(tag+12);
			} else if (!strncasecmp(tag,"ARTIST=",7)) {
				strncpy(dest->artist,tag+7,MAXSTR_FLACHEADER);
				dest->artist[MAXSTR_FLACHEADER]='\0';
			} else if (!strncasecmp(tag,"ALBUM=",6)) {
				strncpy(dest->album,tag+6,MAXSTR_FLACHEADER);
				dest->album[MAXSTR_FLACHEADER]='\0';
			} else if (!strncasecmp(tag,"TITLE=",6)) {
				strncpy(dest->title,tag+6,MAXSTR_FLACHEADER);
				dest->title[MAXSTR_FLACHEADER]='\0';
			} else if (!strncasecmp(tag,"DATE=",5)) {
				strncpy(dest->date,tag+5,MAXSTR_FLACHEADER);
				dest->date[MAXSTR_FLACHEADER]='\0';
			}
			temp[ui]=pre0;
#if 0
			fprintf(stderr,"\tEntry %u bytes: ",ui);
			for (;ui;ui--) { fputc(isprint(temp[0])?temp[0]:'?',stderr); temp++; }
			fputc('\n',stderr);
#else
			temp+=ui;
#endif

			comments--;
		}
#if 1
// assume there is only one comment section
		break;
#endif
		
#if 0
		{
			int i;
			fprintf(stderr,"Comment: ");
			for (i=0;i<len;i++) {
				if (!(i%16)) fputc('\n',stderr);
				fprintf(stderr,"%c",
					isprint(buff[i])?buff[i]:'?');
			}
			fputc('\n',stderr);
		}
#endif
	} else {
		(void)seek_simplebuffer(&simplebuffer,len);
	}

	if (islast) break;
}

if (dest->samplerate) {
	uint64_t samples;
	uint64_t samplerate;
	samples=dest->high_samplecount;
	samples<<=32;
	samples|=dest->low_samplecount;
	samplerate=dest->samplerate;
	dest->duration=(unsigned int)(samples/samplerate);
}
return 0;
error:
	return -1;
}

int read_flacheader(struct flacheader *dest, char *filename) {
reset_flacheader(dest);
if (readheaderfromfile(dest,filename)) GOTOERROR;
return 0;
error:
	return -1;
}

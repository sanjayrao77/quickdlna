/*
 * dump.c - debug network traffic
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
#include <inttypes.h>
#include <stdarg.h>
// #define DEBUG
#include "common/conventions.h"

#include "dump.h"

int upacket_dump(unsigned char *data, uint64_t len, unsigned int towrite, char *tag, char *file, unsigned int line) {
FILE *fout=NULL;
if (!(fout=fopen("/tmp/dump.txt","a"))) GOTOERROR;
fprintf(fout,"%s:%d %s (%"PRIu64"):\n",file,line,tag,len);
if (towrite) {
	if (1!=fwrite(data,towrite,1,fout)) GOTOERROR;
	if (EOF==fputc('\n',fout)) GOTOERROR;
}
if (fclose(fout)) { fout=NULL; GOTOERROR; }
return 0;
error:
	iffclose(fout);
	return -1;
}

int string_dump(char *file, unsigned int line, char *format, ...) {
FILE *fout=NULL;
va_list args;

if (!(fout=fopen("/tmp/dump.txt","a"))) GOTOERROR;
va_start(args, format);

(ignore)fprintf(fout,"%s:%d ",file,line);
(ignore)vfprintf(fout,format,args); // ignore IO errors

va_end(args);
if (fclose(fout)) { fout=NULL; GOTOERROR; }
return 0;
error:
	iffclose(fout);
	return -1;
}

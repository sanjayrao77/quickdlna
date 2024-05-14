/*
 * files.c - try to guess file types
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
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "shared.h"

#include "files.h"

static int checkfile(struct shared *shared, struct file_shared *file) {
struct stat statbuf;
char *filename,*last4;
int len;

filename=file->filename;
len=strlen(filename);
if (len<4) {
	log_shared(shared,0,"error: filename is too short to identify: \"%s\"\n",__FILE__,__LINE__,filename);
	GOTOERROR;
}
last4=filename+len-4;

if (!strncasecmp("flac",last4,4)) {
	file->type=FLAC_TYPE_FILE_SHARED;
} else if (!strncasecmp(".wav",last4,4)) {
	file->type=WAV_TYPE_FILE_SHARED;
} else if (!strncasecmp(".mp3",last4,4)) {
	file->type=MP3_TYPE_FILE_SHARED;
} else if (!strncasecmp(".mp4",last4,4)) {
	file->type=VIDEO_TYPE_FILE_SHARED;
} else {
	log_shared(shared,0,"error: couldn't determine file type: \"%s\"\n",__FILE__,__LINE__,filename);
	GOTOERROR;
}
if (stat(filename,&statbuf)) {
	log_shared(shared,0,"error: couldn't stat file: \"%s\"\n",__FILE__,__LINE__,filename);
	GOTOERROR;
}
file->size=statbuf.st_size;
return 0;
error:
	return -1;
}

int init_files(struct shared *shared) {
struct file_shared **files=shared->files;
unsigned int ui,max=shared->max_files;

for (ui=0;ui<max;ui++) {
	if (checkfile(shared,files[ui])) GOTOERROR;
}
return 0;
error:
	return -1;
}

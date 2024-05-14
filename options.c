/*
 * options.c - user options
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
#include <arpa/inet.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "shared.h"
#include "misc.h"

#include "options.h"

static int addtargetip(struct shared *shared, char *str) {
uint32_t u32;

u32=inet_addr(str);
if (u32==INADDR_NONE) GOTOERROR;
shared->target.ipv4=u32;

return 0;
error:
	return -1;
}

static void addmachine(struct shared *shared, char *str) {
strncpy(shared->server.machine,str,MAX_MACHINE_SHARED);
}
static void addversion(struct shared *shared, char *version) {
strncpy(shared->server.version,version,MAX_VERSION_SHARED);
}
static void addname(struct shared *shared, char *name) {
strncpy(shared->server.friendly,name,MAX_FRIENDLY_SHARED);
}
static void addinstance(struct shared *shared, char *str) {
shared->server.instance=slowtou(str);
}
static void addchildren(struct shared *shared, char *str) {
shared->children.max=slowtou(str);
}

static int allocfiles(struct shared *shared, int max) {
struct file_shared **files;
if (!(files=CALLOC2_blockmem(&shared->blockmem,struct file_shared *,max))) GOTOERROR;
shared->files=files;
shared->max_files=max;
return 0;
error:
	return -1;
}

static int addfile(struct shared *shared, int index, char *filename) {
struct file_shared *file;
if (!(file=ALLOC_blockmem(&shared->blockmem,struct file_shared))) GOTOERROR;

file->size=0;
if (!(file->filename=align64_strdup_blockmem(&shared->blockmem,filename))) GOTOERROR;
file->type=0;
shared->files[index]=file;
return 0;
error:
	return -1;
}

void printusage_options(void) {
fputs("Usage: quickdlna [FILE]..[FILE] [option]..[option] [flag]..[flag]\n",stdout);
fputs("Options:\n",stdout);
fputs("   instance=INT     : allows multiple copies, given different values\n",stdout);
fputs("   children=INT     : allow this many simultaneous requests\n",stdout);
fputs("   targetip=IPV4    : instead of multicast, send only to the given IP\n",stdout);
fputs("   name=STRING      : use XX as the server name\n",stdout);
fputs("   machine=STRING   : use XX as the server type\n",stdout);
fputs("   version=STRING   : use XX as the server version\n",stdout);
fputs("Flags:\n",stdout);
fputs("   --help           : this\n",stdout);
fputs("   --nodiscovery    : don't listen for M-SEARCH broadcasts\n",stdout);
fputs("   --forcediscovery : exit if we can't bind port 1900\n",stdout);
fputs("   --noadvertising  : don't broadcast alive and byebye SSDP\n",stdout);
fputs("   --quiet          : print no messages\n",stdout);
fputs("   --verbose        : print extra messages\n",stdout);
fputs("   --syslog         : print messages to syslog\n",stdout);
fputs("   --background     : run in background, enables --syslog\n",stdout);
fputs("   --mergefiles     : merge all files into one, should be flacs\n",stdout);
}

int init_options(struct shared *shared, int argc, char **argv) {
char *arg;
int idx,filecount=0;

if (allocfiles(shared,argc)) GOTOERROR;

for (idx=1;idx<argc;idx++) {
	arg=argv[idx];
	if (!strncmp(arg,"targetip=",9)) {
		if (addtargetip(shared,arg+9)) GOTOERROR;
	} else if (!strncmp(arg,"version=",8)) {
		(void)addversion(shared,arg+8);
	} else if (!strncmp(arg,"machine=",8)) {
		(void)addmachine(shared,arg+8);
	} else if (!strncmp(arg,"name=",5)) {
		(void)addname(shared,arg+5);
	} else if (!strncmp(arg,"instance=",9)) {
		(void)addinstance(shared,arg+9);
	} else if (!strncmp(arg,"children=",9)) {
		(void)addchildren(shared,arg+9);
	} else if (!strncmp(arg,"--",2)) {
		if (!strcmp(arg,"--help")) {
			printusage_options();
			shared->isquit=1;
		} else if (!strcmp(arg,"--nodiscovery")) {
			shared->options.isnodiscovery=1;
		} else if (!strcmp(arg,"--forcediscovery")) {
			shared->options.isforcediscovery=1;
		} else if (!strcmp(arg,"--noadvertising")) {
			shared->options.isnoadvertising=1;
		} else if (!strcmp(arg,"--verbose")) {
			shared->options.isverbose=1;
		} else if (!strcmp(arg,"--quiet")) {
			shared->options.isquiet=1;
		} else if (!strcmp(arg,"--syslog")) {
			shared->options.issyslog=1;
		} else if (!strcmp(arg,"--background")) {
			shared->options.issyslog=1;
			shared->options.isbackground=1;
		} else if (!strcmp(arg,"--mergefiles")) {
			shared->options.ismergefiles=1;
		} else {
			log_shared(shared,0,"%s:%d unknown argument \"%s\"\n",__FILE__,__LINE__,arg);
			GOTOERROR;
		}

	} else {
		if (addfile(shared,filecount,arg)) GOTOERROR;
		filecount+=1;
	}
}
if (!filecount && !shared->isquit) {
	log_shared(shared,0,"%s:%d no filename specified\n",__FILE__,__LINE__);
	GOTOERROR;
}
shared->max_files=filecount;
return 0;
error:
	return -1;
}

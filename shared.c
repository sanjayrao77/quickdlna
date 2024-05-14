/*
 * shared.c - shared state
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
#include <syslog.h>
#include <stdarg.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"

#include "shared.h"

void clear_shared(struct shared *s) {
static struct shared blank={.udp_socket=-1,.tcp_socket=-1,.children.max=5};
*s=blank;
}

void afterfork_shared(struct shared *s) {
ifclose(s->udp_socket);
ifclose(s->tcp_socket);
}

void deinit_shared(struct shared *s) {
(void)afterfork_shared(s);
iffree(s->buff512);
deinit_blockmem(&s->blockmem);
}

static inline void u32_hexcopy(char *dest, uint32_t u32) {
static char hexchars[16]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;

*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf]; u32=u32>>4; dest++;
*dest=hexchars[u32&0xf];
}

void setuuid_shared(struct shared *s) {
char *dest;
dest=s->server.uuid;
memcpy(dest,"00000000-0000-0000-0000-000000000000",LEN_UUID_SHARED);
u32_hexcopy(dest,s->ipv4_interface);
if (s->server.instance) {
	u32_hexcopy(dest+24,s->server.instance);
}
}

int init_shared(struct shared *s) {
if (!(s->buff512=malloc(512))) GOTOERROR;
if (init_blockmem(&s->blockmem,8192)) GOTOERROR;
strncpy(s->server.version,"Quick/1.0",MAX_VERSION_SHARED);
strncpy(s->server.machine,"quick",MAX_MACHINE_SHARED);
strncpy(s->server.friendly,"quick",MAX_FRIENDLY_SHARED);
return 0;
error:
	return -1;
}

int allocs_shared(struct shared *s) {
if (!(s->children.list=CALLOC2_blockmem(&s->blockmem,struct onechild_shared,s->children.max))) GOTOERROR;
return 0;
error:
	return -1;
}

void log_shared(struct shared *shared, int verbose, char *format, ...) {
va_list args;

if (shared->options.isquiet) return;
if (verbose && !shared->options.isverbose) return;

va_start(args, format);

if (shared->options.issyslog) {
	(void)vsyslog(LOG_ERR,format,args);
} else {
	(ignore)vfprintf(stderr,format,args); // ignore IO errors
}

va_end(args);
}

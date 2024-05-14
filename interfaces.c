/*
 * interfaces.c - find multicast interface ip address
 * Copyright (C) 2023 Sanjay Rao
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
// #define DEBUG
#include "common/conventions.h"

#include "interfaces.h"

CLEARFUNC(interfaces);

int init_interfaces(struct interfaces *p) {
struct ifaddrs *ifs=NULL;

if (getifaddrs(&ifs)) GOTOERROR;
p->ptr=(void *)ifs;
return 0;
error:
	if (ifs) (void)freeifaddrs(ifs);
	return -1;
}

void deinit_interfaces(struct interfaces *p) {
struct ifaddrs *ifs;
ifs=(struct ifaddrs*)p->ptr;
if (!ifs) return;
freeifaddrs(ifs);
}

int print_interfaces(struct interfaces *p, FILE *fout) {
struct ifaddrs *ifs=(struct ifaddrs*)p->ptr;
while (ifs) {
	struct sockaddr_in sa;
	unsigned int flags;
	uint32_t u32;
	if (ifs->ifa_addr->sa_family==AF_INET) {
		fprintf(fout,"%s\n",ifs->ifa_name);
		flags=ifs->ifa_flags;
		if (flags&IFF_UP) fprintf(fout,"\tUP\n");
		if (flags&IFF_LOOPBACK) fprintf(fout,"\tLOOPBACK\n");
		if (flags&IFF_RUNNING) fprintf(fout,"\tRUNNING\n");
		if (flags&IFF_MULTICAST) fprintf(fout,"\tMULTICAST\n");
		memcpy(&sa,ifs->ifa_addr,sizeof(sa));
		u32=sa.sin_addr.s_addr;
		fprintf(fout,"%u.%u.%u.%u\n", (u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff);
	}

	ifs=ifs->ifa_next;
}
return 0;
}

int getipv4multicastip_interfaces(uint32_t *ipv4_out, struct interfaces *p) {
// ipv4=0 if not found
struct ifaddrs *ifs=(struct ifaddrs*)p->ptr;
uint32_t ipv4=0;
while (ifs) {
	struct sockaddr_in sa;
	unsigned int flags;
	if (ifs->ifa_addr->sa_family!=AF_INET) goto next;
	flags=ifs->ifa_flags;
	if (!(flags&IFF_UP)) goto next;
	if (flags&IFF_LOOPBACK) goto next;
	if (!(flags&IFF_RUNNING)) goto next;
	if (!(flags&IFF_MULTICAST)) goto next;
	memcpy(&sa,ifs->ifa_addr,sizeof(sa));
	ipv4=sa.sin_addr.s_addr;
	break;

	next:
	ifs=ifs->ifa_next;
}
*ipv4_out=ipv4;
return 0;
}

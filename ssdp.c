/*
 * ssdp.c - advertise and respond with SSDP, a udp upnp system
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
#include <sys/types.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "shared.h"
#include "dump.h"
#include "misc.h"

#include "ssdp.h"

#define IPV4(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

static int send_message(struct shared *shared, unsigned char *msg, int msglen) {
struct sockaddr_in sa;
int n;

memset(&sa,0,sizeof(sa));
sa.sin_family=AF_INET;
if (shared->target.ipv4) {
	sa.sin_addr.s_addr=shared->target.ipv4;
} else {
	sa.sin_addr.s_addr=IPV4(239,255,255,250);
}
sa.sin_port=htons(1900);

n=sendto(shared->udp_socket,msg,msglen,0,(struct sockaddr*)&sa,sizeof(sa));
if (0>n) GOTOERROR;
if (n!=msglen) GOTOERROR;
UPACKET_DUMP(msg,msglen,"ssdp");

return 0;
error:
	return -1;
}


static int alive_send_ssdp(struct shared *shared, char *maxage, char *nt, char *urn, char *xmlfile) {
unsigned char *buff;
int msglen;
uint32_t u32;

if (!nt) nt=urn;

u32=shared->ipv4_interface;
buff=shared->buff512;
msglen=snprintf((char *)buff,512,
		"NOTIFY * HTTP/1.1\r\n"\
		"HOST:239.255.255.250:1900\r\n"\
		"CACHE-CONTROL:max-age:%s\r\n"\
		"LOCATION:http://%u.%u.%u.%u:%u/%s\r\n"\
		"SERVER: %s DLNADOC/1.50 UPnP/1.0 OneDLNA/0.1\r\n"\
		"NT:%s\r\n"\
		"USN:uuid:%s%s%s\r\n"\
		"NTS:ssdp:alive\r\n"\
		"\r\n",
		maxage,
		(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff, shared->tcp_port, xmlfile,
		shared->server.version,
		nt,
		shared->server.uuid,
		urn?"::":"",
		urn?urn:"");
if (send_message(shared,buff,msglen)) GOTOERROR;
return 0;
error:
	return -1;
}

int alives_send_ssdp(struct shared *shared, char *maxage) {
char uuidstr[6+LEN_UUID_SHARED];

memcpy(uuidstr,"uuid:",5);
memcpy(uuidstr+5,shared->server.uuid,LEN_UUID_SHARED);
uuidstr[5+LEN_UUID_SHARED]=0;
if (alive_send_ssdp(shared,maxage,uuidstr,NULL,"root.xml.0")) GOTOERROR;
if (alive_send_ssdp(shared,maxage,NULL,"upnp:rootdevice","root.xml.1")) GOTOERROR;
if (alive_send_ssdp(shared,maxage,NULL,"urn:schemas-upnp-org:device:MediaServer:1","root.xml.2")) GOTOERROR;
if (alive_send_ssdp(shared,maxage,NULL,"urn:schemas-upnp-org:service:ContentDirectory:1","root.xml.3")) GOTOERROR;
return 0;
error:
	return -1;
}

static int byebye_send_ssdp(struct shared *shared, char *nt, char *urn) {
unsigned char *buff;
int msglen;

if (!nt) nt=urn;

buff=shared->buff512;
msglen=snprintf((char *)buff,512,
		"NOTIFY * HTTP/1.1\r\n"\
		"HOST:239.255.255.250:1900\r\n"\
		"NT:%s\r\n"\
		"USN:uuid:%s%s%s\r\n"\
		"NTS:ssdp:byebye\r\n"\
		"\r\n",
		nt,
		shared->server.uuid,
		urn?"::":"",
		urn?urn:"");
if ((msglen<0)||(msglen>=512)) GOTOERROR;

if (send_message(shared,buff,msglen)) GOTOERROR;
return 0;
error:
	return -1;
}

int byebyes_send_ssdp(struct shared *shared) {
char uuidstr[6+LEN_UUID_SHARED];

memcpy(uuidstr,"uuid:",5);
memcpy(uuidstr+5,shared->server.uuid,LEN_UUID_SHARED);
uuidstr[5+LEN_UUID_SHARED]=0;
if (byebye_send_ssdp(shared,uuidstr,NULL)) GOTOERROR;
if (byebye_send_ssdp(shared,NULL,"upnp:rootdevice")) GOTOERROR;
if (byebye_send_ssdp(shared,NULL,"urn:schemas-upnp-org:device:MediaServer:1")) GOTOERROR;
if (byebye_send_ssdp(shared,NULL,"urn:schemas-upnp-org:service:ContentDirectory:1")) GOTOERROR;
return 0;
error:
	return -1;
}

int getsocket_ssdp(struct shared *shared) {
int fd=-1;
if (0>(fd=socket(AF_INET,SOCK_DGRAM,0))) GOTOERROR;

{
	struct sockaddr_in sa;
	int retries=0;

	memset(&sa,0,sizeof(sa));
	sa.sin_family=AF_INET;
	if (!shared->options.isnodiscovery) {
		sa.sin_port=htons(1900);
	}
	while (1) {
		if (0<=bind(fd,(struct sockaddr*)&sa,sizeof(sa))) break;
		if (errno==EADDRINUSE) {
			if (!retries && !shared->options.isnodiscovery) {
				if (!shared->options.isforcediscovery) {
					sa.sin_port=0;
					shared->options.isnodiscovery=1;
					log_shared(shared,0,"%s:%d Disabling SSDP discovery\n",__FILE__,__LINE__);
					continue;
				} else {
					log_shared(shared,0,"%s:%d SSDP port is already in use\n",__FILE__,__LINE__);
					log_shared(shared,1,"%s:%d You can use --nodiscovery to disable this.\n",__FILE__,__LINE__);
				}
			}
			retries++;
			if (retries==30) {
				log_shared(shared,0,"%s:%d error: couldn't bind UDP port, giving up\n",__FILE__,__LINE__);
				GOTOERROR;
			}
			sleep(10);
			continue;
		} // EADDRINUSE
		GOTOERROR;
	}
	if (shared->options.isverbose) {
		socklen_t ssa;
		ssa=sizeof(sa);
		if (getsockname(fd,(struct sockaddr*)&sa,&ssa)) GOTOERROR;
		if (ssa==sizeof(sa)) {
			log_shared(shared,1,"%s:%d bound on port %u\n",__FILE__,__LINE__,ntohs(sa.sin_port));
		}
	}
}

if (!shared->options.isnodiscovery) {
	struct in_addr iface;
	iface.s_addr=shared->ipv4_interface;
	if (0>setsockopt(fd,IPPROTO_IP,IP_MULTICAST_IF,(char *)&iface,sizeof(iface))) GOTOERROR;

	struct ip_mreq ipm;
	ipm.imr_multiaddr.s_addr=IPV4(239,255,255,250);
	ipm.imr_interface.s_addr=shared->ipv4_interface;
	if (0>setsockopt(fd,IPPROTO_IP,IP_ADD_MEMBERSHIP,(char *)&ipm,sizeof(ipm))) GOTOERROR;
}

shared->udp_socket=fd;
return 0;
error:
	ifclose(fd);
	return -1;
}

static int sendmsearchreply(struct shared *shared, uint32_t ipv4_dest, unsigned short nport_dest) {
unsigned char *msg;
char datestr[30];
uint32_t ipv4_src;
int n,msglen;

ipv4_src=shared->ipv4_interface;
(void)httpctime_misc(datestr,time(NULL));

msg=shared->buff512;
msglen=snprintf((char *)msg,512,
		"HTTP/1.1 200 OK\r\n"\
		"CACHE-CONTROL: max-age=900\r\n"\
		"DATE: %s\r\n"\
		"ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"\
		"USN: uuid:%s::urn:schemas-upnp-org:device:MediaServer:1\r\n"\
		"EXT:\r\n"\
		"SERVER: %s DLNADOC/1.50 UPnP/1.0 %s\r\n"\
		"LOCATION: http://%u.%u.%u.%u:%u/root.xml.m\r\n"\
		"CONTENT-LENGTH: 0\r\n"\
		"\r\n",
		datestr,
		shared->server.uuid,
		shared->server.machine,shared->server.version,
		(ipv4_src)&0xff, (ipv4_src>>8)&0xff, (ipv4_src>>16)&0xff, (ipv4_src>>24)&0xff, shared->tcp_port
		);
if ((msglen<0)||(msglen>=512)) GOTOERROR;
#if 0
#warning
	fprintf(stderr,"%s:%d sending m-search reply:\n%s\n",__FILE__,__LINE__,(char *)msg);
#endif

struct sockaddr_in sa;
memset(&sa,0,sizeof(sa));
sa.sin_family=AF_INET;
sa.sin_addr.s_addr=ipv4_dest;
sa.sin_port=nport_dest;
n=sendto(shared->udp_socket,msg,msglen,0,(struct sockaddr*)&sa,sizeof(sa));
if (0>n) GOTOERROR;
if (n!=msglen) GOTOERROR;
UPACKET_DUMP(msg,msglen,"msearch reply");

return 0;
error:
	return -1;
}

int checkclient_ssdp(struct shared *shared) {
struct sockaddr_in sa;
socklen_t ssa=sizeof(sa);
unsigned char *buff512;
uint32_t u32;
int k;
char *st=NULL,*man=NULL;

buff512=shared->buff512;
k=recvfrom(shared->udp_socket,buff512,511,0,(struct sockaddr*)&sa,&ssa);
if (k<=0) return 0;
UPACKET_DUMP(buff512,k,"broadcast");
if (memcmp(buff512,"M-SEARCH * HTTP/1.1",19)) return 0;
buff512[k]=0;
u32=sa.sin_addr.s_addr;
#if 0
#warning
	if (shared->target.ipv4 && (shared->target.ipv4==u32)) {
		fprintf(stderr,"%s:%d got packet from %u.%u.%u.%u, %d bytes:\n%s\n",__FILE__,__LINE__,
			(u32>>0)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff, k, buff512);
	} else {
		fprintf(stderr,"%s:%d got packet from %u.%u.%u.%u, %d bytes\n",__FILE__,__LINE__,
			(u32>>0)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff, k);
	}
#endif
if (shared->target.ipv4 && (shared->target.ipv4!=u32)) return 0;
{
	char *line;
	line=(char *)buff512;
	while (1) {
		char *next;
		next=strchr(line,'\n');
		if (next) *next=0;

		if (!strncmp(line,"ST:",3)) {
			st=line+3;
		} else if (!strncmp(line,"MAN:",4)) {
			man=line+4;
		} else {
//			fprintf(stderr,"%s:%d ignoring line \"%s\"\n",__FILE__,__LINE__,line);
		}

		if (!next) break;
		line=next+1;
	}
}
if (!st) return 0;
if (!man) return 0;
for (;isspace(*st);st++);
for (;isspace(*man);man++);
if (strncmp(st,"urn:schemas-upnp-org:device:MediaServer:1",41)) {
//	fprintf(stderr,"%s:%d st didn't match \"%s\"\n",__FILE__,__LINE__,st);
	return 0;
}
if (strncmp(man,"\"ssdp:discover\"",15)) {
//	fprintf(stderr,"%s:%d man didn't match \"%s\"\n",__FILE__,__LINE__,man);
	return 0;
}

if (sendmsearchreply(shared,u32,sa.sin_port)) GOTOERROR;

return 0;
error:
	return -1;
}

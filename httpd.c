/*
 * httpd.c - reply to http requests
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
#include <time.h>
#include <sys/types.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "shared.h"
#include "misc.h"
#include "dump.h"
#include "icon.h"
#include "lineio.h"
#include "flacheader.h"
#include "xml.h"

#include "httpd.h"

int getsocket_httpd(struct shared *shared) {
struct sockaddr_in sa;
socklen_t ssa;
int fd=-1;

if (0>(fd=socket(AF_INET,SOCK_STREAM,0))) GOTOERROR;

memset(&sa,0,sizeof(sa));
sa.sin_family=AF_INET;
sa.sin_addr.s_addr=shared->ipv4_interface;
if (0>bind(fd,(struct sockaddr*)&sa,sizeof(sa))) GOTOERROR;

ssa=sizeof(sa);
if (getsockname(fd,(struct sockaddr*)&sa,&ssa)) GOTOERROR;
if (ssa!=sizeof(sa)) GOTOERROR;
shared->tcp_port=ntohs(sa.sin_port);
log_shared(shared,1,"%s:%d bound on port %u\n",__FILE__,__LINE__,ntohs(sa.sin_port));

if (listen(fd,5)) GOTOERROR;

shared->tcp_socket=fd;
return 0;
error:
	ifclose(fd);
	return -1;
}

struct replybuffer {
#define SIZE_CONTENTTYPE_REPLYBUFFER	64
	char contenttype[SIZE_CONTENTTYPE_REPLYBUFFER];
	int replycode; char *replycodemsg; // NULL => 200 or 206
	unsigned char *buff;
	unsigned int bufflen;
	int isexternal;
	struct {
		unsigned int left;
		unsigned char *cursor;
	} internal;
	struct {
		int fd;
	} external;
	uint64_t fullsize;
	int isrange;
	struct {
		uint64_t start,limit;
	} range;
	int iserror;
#ifdef DEBUG
	struct {
		char *replyheader;
		int isshortreply;
		uint64_t byteswritten;
	} debug;
#endif
};
static void add404_replybuffer(struct replybuffer *rb);

static inline void clear_replybuffer(struct replybuffer *rb) {
static struct replybuffer blank={.external.fd=-1};
*rb=blank;
}

static inline void reset_replybuffer(struct replybuffer *rb) {
rb->internal.cursor=rb->buff;
rb->internal.left=rb->bufflen;
}

static int init_replybuffer(struct replybuffer *rb, unsigned int size) {
if (!(rb->buff=malloc(size))) GOTOERROR;
rb->internal.cursor=rb->buff;
rb->bufflen=size;
rb->internal.left=size;
return 0;
error:
	return -1;
}

static void addustring_replybuffer(struct replybuffer *rb, unsigned char *msg, unsigned int msglen) {
if (rb->internal.left<msglen) {
	rb->iserror=1;
	return;
}
memcpy(rb->internal.cursor,msg,msglen);
rb->internal.cursor+=msglen;
rb->internal.left-=msglen;
}
#define addstring_replybuffer(a,b) addustring_replybuffer(a,(unsigned char *)b,strlen(b))

static void adduint_replybuffer(struct replybuffer *rb, unsigned int ui) {
char buff[16];
snprintf(buff,16,"%u",ui);
addstring_replybuffer(rb,buff);
}
static void adduint64_replybuffer(struct replybuffer *rb, uint64_t u64) {
char buff[32];
snprintf(buff,32,"%"PRIu64,u64);
addstring_replybuffer(rb,buff);
}

static int addraw_replybuffer(struct replybuffer *rb, unsigned char *data, unsigned int datalen, char *mimetype) {
addustring_replybuffer(rb,data,datalen);
if (mimetype) {
	snprintf(rb->contenttype,SIZE_CONTENTTYPE_REPLYBUFFER,"Content-Type: %s\r\n",mimetype);
}
return 0;
}

static int addmerge_replybuffer(struct shared *shared, struct replybuffer *rb, int isrange, uint64_t rangestart, uint64_t rangelimit) {
uint64_t u64=0;
unsigned int idx;

snprintf(rb->contenttype,SIZE_CONTENTTYPE_REPLYBUFFER,"Content-Type: audio/x-flac\r\n");

rb->isexternal=1;

for (idx=0;idx<shared->max_files;idx++) {
	struct file_shared *file;
	file=shared->files[idx];
	u64+=file->size;
}


rb->fullsize=u64;

if (isrange) { // we only support open-ended "xx-" range
	if (rangestart>u64) {
		(void)add404_replybuffer(rb); // TODO send correct error
		return 0;
	}
#ifdef DEBUG
		fprintf(stderr,"%s:%d streaming from offset %"PRIu64" in %s\n",__FILE__,__LINE__,rangestart,"merge");
#endif
} else {
#ifdef DEBUG
		fprintf(stderr,"%s:%d pid:%d streaming %s\n",__FILE__,__LINE__,getpid(),"merge");
#endif
}

return 0;
}

static int addfile_replybuffer(struct replybuffer *rb, char *filename, char *mimetype, int isrange,
		uint64_t rangestart, uint64_t rangelimit) {
struct stat statbuf;
int fd=-1;
uint64_t u64;

if (mimetype) {
	snprintf(rb->contenttype,SIZE_CONTENTTYPE_REPLYBUFFER,"Content-Type: %s\r\n",mimetype);
}

rb->isexternal=1;

if (0>(fd=open(filename,O_RDONLY))) GOTOERROR;
if (fstat(fd,&statbuf)) GOTOERROR;

rb->fullsize=u64=statbuf.st_size;

if (isrange) { // we only support open-ended "xx-" range
	if (rangestart>u64) {
		(void)add404_replybuffer(rb); // TODO send correct error
		return 0;
	}
#ifdef DEBUG
		fprintf(stderr,"%s:%d streaming from offset %"PRIu64" in %s\n",__FILE__,__LINE__,rangestart,filename);
#endif
	rb->external.fd=fd;
} else {
#ifdef DEBUG
		fprintf(stderr,"%s:%d pid:%d streaming file %s\n",__FILE__,__LINE__,getpid(),filename);
#endif
	rb->external.fd=fd;
}

return 0;
error:
	ifclose(fd);
	return -1;
}

static int make_rootxml(struct shared *shared, struct replybuffer *rb) {

addstring_replybuffer(rb,"<?xml version=\"1.0\"?>\r\n");
addstring_replybuffer(rb,"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">");
addstring_replybuffer(rb,"<specVersion><major>1</major><minor>0</minor></specVersion>");
addstring_replybuffer(rb,"<device>");

addstring_replybuffer(rb,"<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>");
addstring_replybuffer(rb,shared->server.friendly);
addstring_replybuffer(rb,"</friendlyName><manufacturer>manufacturer</manufacturer><manufacturerURL>manufacturerurl</manufacturerURL><modelDescription>modeldescription</modelDescription><modelName>modelname</modelName><modelNumber>1</modelNumber><modelURL>modelurl</modelURL><serialNumber>serialnumber</serialNumber><UDN>uuid:");
addustring_replybuffer(rb,(unsigned char *)shared->server.uuid,LEN_UUID_SHARED);
addstring_replybuffer(rb,"</UDN><dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMS-1.50</dlna:X_DLNADOC><presentationURL>/</presentationURL>");

addstring_replybuffer(rb,"<iconList><icon><mimetype>image/png</mimetype><width>120</width><height>120</height><depth>24</depth><url>/icon.png.120</url></icon></iconList>");

addstring_replybuffer(rb,"<serviceList>");

addstring_replybuffer(rb,"<service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><controlURL>/ctl/ContentDir</controlURL><eventSubURL>/evt/ContentDir</eventSubURL><SCPDURL>/ContentDir.xml</SCPDURL></service>");

addstring_replybuffer(rb,"</serviceList>");
addstring_replybuffer(rb,"</device></root>");

snprintf(rb->contenttype,SIZE_CONTENTTYPE_REPLYBUFFER,"Content-Type: text/xml; charset=\"utf-8\"\r\n");

return 0;
}

static void addxmlstring_replybuffer(struct replybuffer *rb, char *str) {
while (1) {
	switch (*str) {
		case 0: return;
		case '<': addstring_replybuffer(rb,"&amp;lt;"); break;
		case '>': addstring_replybuffer(rb,"&amp;gt;"); break;
		case '&': addstring_replybuffer(rb,"&amp;amp;"); break;
		default:
			if (isprint(*str)) addustring_replybuffer(rb,(unsigned char *)str,1);
			else addustring_replybuffer(rb,(unsigned char *)".",1);
	}
	str++;
}
}

static void addxmlfilename_replybuffer(struct replybuffer *rb, unsigned char idx, char *filename) {
char prefix[10],*bn;
snprintf(prefix,10,"%u. ",idx+1);
addstring_replybuffer(rb,prefix);
bn=basename(filename);
addxmlstring_replybuffer(rb,bn);
}

struct request {
#define ROOTXML_FILEINDEX_REQUEST	1
#define ICONPNG_FILEINDEX_REQUEST	2
#define CONTENTDIR_FILEINDEX_REQUEST	3
#define ONEFLAC_FILEINDEX_REQUEST	4
#define ONEWAV_FILEINDEX_REQUEST	5
#define ONEMP3_FILEINDEX_REQUEST	6
#define ONEMP4_FILEINDEX_REQUEST	7
#define MERGE_FILEINDEX_REQUEST	8
	int fileindex;
	unsigned int postlen;
#define MAX_SOAPACTION_REQUEST 79
	char soapaction[MAX_SOAPACTION_REQUEST+1];
	int isrange;
	uint64_t rangestart,rangelimit;
	struct file_shared *file;
#ifdef DEBUG
	struct {
		char *request;
		unsigned char *post;
	} debug;
#endif
};
SICLEARFUNC(request);

static void addduration_replybuffer(struct replybuffer *rb, unsigned int seconds) {
//						1:00:00.000
unsigned int minutes,hours;
unsigned char buff2[2];

hours=seconds/3600;
seconds=seconds%3600;
minutes=seconds/60;
seconds=seconds%60;

adduint_replybuffer(rb,hours);
addustring_replybuffer(rb,(unsigned char *)":",1);

buff2[0]='0'+(minutes/10);
buff2[1]='0'+(minutes%10);
addustring_replybuffer(rb,buff2,2);

addustring_replybuffer(rb,(unsigned char *)":",1);

buff2[0]='0'+(seconds/10);
buff2[1]='0'+(seconds%10);
addustring_replybuffer(rb,buff2,2);

addustring_replybuffer(rb,(unsigned char *)".000",4);

}

static int mergefiles_browse(struct shared *shared, struct replybuffer *rb) {
// this is to get around behavior on my Express, probably a bug on the Express?
(void)reset_replybuffer(rb);

addstring_replybuffer(rb,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
addstring_replybuffer(rb,"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">");
addstring_replybuffer(rb,"<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>");

addstring_replybuffer(rb,"&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:pv=\"http://www.pv.com/pvns/\"&gt;");

addstring_replybuffer(rb,"&lt;item id=\"");
adduint_replybuffer(rb,1);
addstring_replybuffer(rb,"\" parentID=\"0\" restricted=\"1\"&gt;");
addstring_replybuffer(rb,"&lt;dc:title&gt;");
addstring_replybuffer(rb,"merged files");
addstring_replybuffer(rb,"&lt;/dc:title&gt;");
addstring_replybuffer(rb,"&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;");

{
	uint64_t mergesize=0;
	unsigned int duration=0;
	unsigned int idx;
	struct flacheader flacheader;

	for (idx=0;idx<shared->max_files;idx++) {
		struct file_shared *file;

		file=shared->files[idx];
		mergesize+=file->size;
		if (read_flacheader(&flacheader,file->filename)) {
			log_shared(shared,1,"%s:%d error reading flacheader for \"%s\"\n",__FILE__,__LINE__,file->filename);
			duration+=600;
		} else {
			duration+=flacheader.duration;
		}
	}
	addstring_replybuffer(rb,"&lt;res size=\""); adduint64_replybuffer(rb,mergesize);
	addstring_replybuffer(rb,"\" duration=\"");
	addduration_replybuffer(rb,duration);
	addstring_replybuffer(rb,"\" bitrate=\"");
	addstring_replybuffer(rb,"100000");
	addstring_replybuffer(rb,"\" protocolInfo=\"http-get:*:audio/x-flac:*\"&gt;");
}

{
	char *buff=(char *)shared->buff512;
	uint32_t u32=shared->ipv4_interface;
	snprintf(buff,512,"http://%u.%u.%u.%u:%u/merge.0",
		(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff,shared->tcp_port);
	addstring_replybuffer(rb,buff);
}
addstring_replybuffer(rb,"&lt;/res&gt;&lt;/item&gt;");

addstring_replybuffer(rb,"&lt;/DIDL-Lite&gt;");
addstring_replybuffer(rb,"</Result>");
addstring_replybuffer(rb,"<NumberReturned>");
adduint_replybuffer(rb,1);
addstring_replybuffer(rb,"</NumberReturned><TotalMatches>");
adduint_replybuffer(rb,1);
addstring_replybuffer(rb,"</TotalMatches><UpdateID>0</UpdateID></u:BrowseResponse></s:Body></s:Envelope>\r\n");
return 0;
}

static int getbrowsevars(unsigned int *start_out, unsigned int *max_out, struct shared *shared, char *data_in, unsigned int datalen) {
struct xml xml;
unsigned char *data=(unsigned char *)data_in;
struct tag_xml envelope,body,browse,startingindex,requestedcount;
int start=-1,max=-1;
char *temp;
int ret=0;

clear_xml(&xml);
(void)set_tag_xml(&envelope,&xml.top,"Envelope");
(void)set_tag_xml(&body,&envelope,"Body");
(void)set_tag_xml(&browse,&body,"Browse");
(void)set_tag_xml(&startingindex,&browse,"StartingIndex");
(void)set_tag_xml(&requestedcount,&browse,"RequestedCount");

(void)removecomments_xml(&datalen,data);
data[datalen]=0;
if (parse_xml(&xml,data,datalen)) {
	ret=-1;
	log_shared(shared,1,"%s:%d error parsing xml browse request\n",__FILE__,__LINE__);
} else {
	if (startingindex.value.ustr) {
		start=slowtou((char *)startingindex.value.ustr);
	} else {
		log_shared(shared,1,"%s:%d didn't find StartingIndex in xml browse request\n",__FILE__,__LINE__);
		ret=-2;
	}
	if (requestedcount.value.ustr) {
		max=slowtou((char *)requestedcount.value.ustr);
	} else {
		log_shared(shared,1,"%s:%d didn't find RequestedCount in xml browse request\n",__FILE__,__LINE__);
		ret=-3;
	}
}
if (start<0) {
	temp=strstr(data_in,"<StartingIndex>");
	if (temp) {
		for (temp+=15;isspace(*temp);temp++);
		start=slowtou(temp);
	} else {
		start=0;
	}
}
if (max<0) {
	temp=strstr(data_in,"<RequestedCount>");
	if (temp) {
		for (temp+=16;isspace(*temp);temp++);
		max=slowtou(temp);
	} else {
		max=10;
	}
}

#ifdef DEBUG
	fprintf(stderr,"%s:%d got browse start:%d, max:%d\n",__FILE__,__LINE__,start,max);
#endif
*start_out=(unsigned int)start;
*max_out=(unsigned int)max;
return ret;
}


static int handle_browse(struct shared *shared, struct request *request, struct replybuffer *rb) {
unsigned int browse_start=0,browse_max=10,browse_limit;
unsigned int itemcount=0;

if (!strstr(request->soapaction,"#Browse")) {
#ifdef DEBUG
	fprintf(stderr,"%s:%d unknown soapaction: \"%s\"\n",__FILE__,__LINE__,request->soapaction);
#endif
	GOTOERROR;
}

if (shared->options.ismergefiles) {
	return mergefiles_browse(shared,rb);
}

// POST is in replybuffer.buff, 0-term
(ignore)getbrowsevars(&browse_start,&browse_max,shared,(char *)rb->buff,request->postlen);

(void)reset_replybuffer(rb);

addstring_replybuffer(rb,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
addstring_replybuffer(rb,"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">");
addstring_replybuffer(rb,"<s:Body><u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>");

addstring_replybuffer(rb,"&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:pv=\"http://www.pv.com/pvns/\"&gt;");

browse_limit=browse_start+browse_max;
if (browse_limit>shared->max_files) browse_limit=shared->max_files;

unsigned int idx;
for (idx=browse_start;idx<browse_limit;idx++) {
	struct file_shared *file;

	file=shared->files[idx];
	itemcount+=1;

	if (file->type&MUSICMASK_TYPE_FILE_SHARED) {
		char *prefix="null";
		addstring_replybuffer(rb,"&lt;item id=\"");
		adduint_replybuffer(rb,idx+1);
		addstring_replybuffer(rb,"\" parentID=\"0\" restricted=\"1\"&gt;");
		addstring_replybuffer(rb,"&lt;dc:title&gt;");
		addxmlfilename_replybuffer(rb,idx,file->filename);
		addstring_replybuffer(rb,"&lt;/dc:title&gt;");
		addstring_replybuffer(rb,"&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;");
		switch (file->type) {
			case FLAC_TYPE_FILE_SHARED:
				prefix="flac";
				{
					struct flacheader flacheader;
					if (read_flacheader(&flacheader,file->filename)) {
						log_shared(shared,1,"%s:%d error reading flacheader for %s\n",__FILE__,__LINE__,file->filename);
						addstring_replybuffer(rb,"&lt;res size=\""); adduint64_replybuffer(rb,file->size);
						addstring_replybuffer(rb,"\" duration=\"1:00:00.000\" bitrate=\"100000\" protocolInfo=\"http-get:*:audio/x-flac:*\"&gt;");
					} else {
						addstring_replybuffer(rb,"&lt;dc:description&gt;");
							addxmlstring_replybuffer(rb,flacheader.title); addstring_replybuffer(rb,"&lt;/dc:description&gt;");
						addstring_replybuffer(rb,"&lt;dc:date&gt;");
							addxmlstring_replybuffer(rb,flacheader.date); addstring_replybuffer(rb,"&lt;/dc:date&gt;");
						addstring_replybuffer(rb,"&lt;upnp:artist&gt;");
							addxmlstring_replybuffer(rb,flacheader.artist); addstring_replybuffer(rb,"&lt;/upnp:artist&gt;");
						addstring_replybuffer(rb,"&lt;upnp:album&gt;");
							addxmlstring_replybuffer(rb,flacheader.album); addstring_replybuffer(rb,"&lt;/upnp:album&gt;");
						addstring_replybuffer(rb,"&lt;upnp:originalTrackNumber&gt;");
							adduint_replybuffer(rb,flacheader.tracknumber); addstring_replybuffer(rb,"&lt;/upnp:originalTrackNumber&gt;");
						addstring_replybuffer(rb,"&lt;res size=\""); adduint64_replybuffer(rb,file->size);
						addstring_replybuffer(rb,"\" duration=\"");
						addduration_replybuffer(rb,flacheader.duration);
						addstring_replybuffer(rb,"\" bitrate=\"");
						if (!flacheader.duration) addstring_replybuffer(rb,"100000");
						else adduint_replybuffer(rb,8*(unsigned int)(file->size/(uint64_t)flacheader.duration));
						addstring_replybuffer(rb,"\" protocolInfo=\"http-get:*:audio/x-flac:*\"&gt;");
					}
				}
				break;
			case WAV_TYPE_FILE_SHARED:
				prefix="wav";
				addstring_replybuffer(rb,"&lt;res size=\""); adduint64_replybuffer(rb,file->size);
				addstring_replybuffer(rb,"\" duration=\"1:00:00.000\" bitrate=\"100000\" protocolInfo=\"http-get:*:audio/x-wav:*\"&gt;");
				break;
			case MP3_TYPE_FILE_SHARED:
				prefix="mp3";
				addstring_replybuffer(rb,"&lt;res size=\""); adduint64_replybuffer(rb,file->size);
				addstring_replybuffer(rb,"\" duration=\"1:00:00.000\" bitrate=\"100000\" protocolInfo=\"http-get:*:audio/mpeg:*\"&gt;");
				break;
		}
		{
			char *buff=(char *)shared->buff512;
			uint32_t u32=shared->ipv4_interface;
			snprintf(buff,512,"http://%u.%u.%u.%u:%u/%s.%u",
				(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff,shared->tcp_port,
				prefix,idx);
			addstring_replybuffer(rb,buff);
		}
		addstring_replybuffer(rb,"&lt;/res&gt;&lt;/item&gt;");
	} else if (file->type==VIDEO_TYPE_FILE_SHARED) {
		addstring_replybuffer(rb,"&lt;item id=\"");
		adduint_replybuffer(rb,idx+1);
		addstring_replybuffer(rb,"\" parentID=\"0\" restricted=\"1\"&gt;");
		addstring_replybuffer(rb,"&lt;dc:title&gt;");
		addxmlfilename_replybuffer(rb,idx,file->filename);
		addstring_replybuffer(rb,"&lt;/dc:title&gt;");
		addstring_replybuffer(rb,"&lt;upnp:class&gt;object.item.videoItem&lt;/upnp:class&gt;");
		addstring_replybuffer(rb,"&lt;res size=\"");
		adduint64_replybuffer(rb,file->size);
		addstring_replybuffer(rb,"\" duration=\"5:00:00.000\" resolution=\"100x100\" protocolInfo=\"http-get:*:video/mp4:*\"&gt;");
		{
			char *buff=(char *)shared->buff512;
			uint32_t u32=shared->ipv4_interface;
			snprintf(buff,512,"http://%u.%u.%u.%u:%u/mp4.%u",
				(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff,shared->tcp_port,
				idx);
			addstring_replybuffer(rb,buff);
		}
		addstring_replybuffer(rb,"&lt;/res&gt;&lt;/item&gt;");
	} else {
		GOTOERROR;
	}
}

addstring_replybuffer(rb,"&lt;/DIDL-Lite&gt;");
addstring_replybuffer(rb,"</Result>");
addstring_replybuffer(rb,"<NumberReturned>");
adduint_replybuffer(rb,itemcount);
addstring_replybuffer(rb,"</NumberReturned><TotalMatches>");
adduint_replybuffer(rb,shared->max_files);
addstring_replybuffer(rb,"</TotalMatches><UpdateID>0</UpdateID></u:BrowseResponse></s:Body></s:Envelope>\r\n");

return 0;
error:
	return -1;
}

static int parserange(struct request *req, char *str) {
uint64_t start;

while(isspace(*str)) str++;
if (strncasecmp(str,"bytes=",6)) GOTOERROR;
str+=6;
start=slowtou64(str);
str=strchr(str,'-');
if (!str) GOTOERROR;
str++;
req->rangestart=start;
if (*str) {
	uint64_t limit;
	limit=slowtou64(str);
	if (limit<start) GOTOERROR;
	limit++;
	req->rangelimit=limit;
}
return 0;
error:
	return -1;
}

static struct file_shared *getfile(struct shared *shared, char *str) {
struct file_shared *file;
unsigned int u32;
u32=slowtou(str);
if (u32>=shared->max_files) GOTOERROR;
file=shared->files[u32];
return file;
error:
	return NULL;
}

static void add404_replybuffer(struct replybuffer *rb) {
(void)reset_replybuffer(rb);
rb->isrange=0; rb->isexternal=0;
rb->replycode=404;
rb->replycodemsg="Not Found";
strcpy(rb->contenttype,"Content-Type: text/plain\r\n");
addstring_replybuffer(rb,"File not found");
rb->fullsize=rb->bufflen-rb->internal.left;
}

static void add500_replybuffer(struct replybuffer *rb) {
(void)reset_replybuffer(rb);
rb->isrange=0; rb->isexternal=0;
rb->replycode=500;
rb->replycodemsg="Server Error";
strcpy(rb->contenttype,"Content-Type: text/plain\r\n");
addstring_replybuffer(rb,"Internal server error");
rb->fullsize=rb->bufflen-rb->internal.left;
}

static int getrequest(int *istimeout_errorout, struct shared *shared, struct request *request, int fd, struct lineio *lineio,
		time_t expires) {
// lineio is using shared.buff512, don't use buff512 here
int istimeouterror=0;

while (1) {
	unsigned int linelen;
	char *line;
	line=(char *)gets_lineio(&istimeouterror,&linelen,lineio,fd,expires);
	if (!line) {
		if (istimeouterror) goto error;
		GOTOERROR;
	} else {
		if (!linelen) GOTOERROR; // can't happen
		linelen--;
		line[linelen]=0;
		if (linelen) {
			linelen--;
			if (line[linelen]=='\r') line[linelen]=0;
		}
	}
	if (!line[0]) break;
	
	PACKET_DUMP("",line);
	if (!memcmp("GET ",line,4)) {
		char *linep4=line+4;
#ifdef DEBUG
		fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,line);
#endif
		if (!strncmp(linep4,"/root.xml",9)) request->fileindex=ROOTXML_FILEINDEX_REQUEST;
		else if (!strncmp(linep4,"/icon.png",9)) request->fileindex=ICONPNG_FILEINDEX_REQUEST;
		else if (!strncmp(linep4,"/flac.",6)) {
			request->fileindex=ONEFLAC_FILEINDEX_REQUEST;
			request->file=getfile(shared,linep4+6);
		} else if (!strncmp(linep4,"/wav.",5)) {
			request->fileindex=ONEWAV_FILEINDEX_REQUEST;
			request->file=getfile(shared,linep4+5);
		} else if (!strncmp(linep4,"/mp3.",5)) {
			request->fileindex=ONEMP3_FILEINDEX_REQUEST;
			request->file=getfile(shared,linep4+5);
		} else if (!strncmp(linep4,"/mp4.",5)) {
			request->fileindex=ONEMP4_FILEINDEX_REQUEST;
			request->file=getfile(shared,linep4+5);
		} else if (!strncmp(linep4,"/contentdir.browse",18)) {
			strcpy(request->soapaction,"#Browse");
			request->fileindex=CONTENTDIR_FILEINDEX_REQUEST;
		} else if (!strncmp(linep4,"/merge.",7)) {
			request->fileindex=MERGE_FILEINDEX_REQUEST;
		} else {
			log_shared(shared,1,"%s:%d unhandled header: %s\n",__FILE__,__LINE__,line);
			GOTOERROR;
		}
	} else if (!memcmp("POST",line,4)) {
		char *linep5=line+5;
#ifdef DEBUG
		fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,line);
#endif
		if (!strncmp(linep5,"/ctl/ContentDir",15)) request->fileindex=CONTENTDIR_FILEINDEX_REQUEST;
		else {
			log_shared(shared,1,"%s:%d unhandled header: %s\n",__FILE__,__LINE__,line);
			GOTOERROR;
		}
	} else if (!strncasecmp("content-",line,8)) {
		char *linep8=line+8;
		if (!strncasecmp(linep8,"length:",7)) {
			char *temp=linep8+7;
			while (isspace(*temp)) temp++;
			request->postlen=slowtou(temp);
		} else if (!strncasecmp(linep8,"type:",5)) {
// we should get text/xml for POST upnp requests, we could check that if we ever read it
//			fprintf(stderr,"%s:%d content type: \"%s\"\n",__FILE__,__LINE__,linep8+5);
		} else {
			log_shared(shared,1,"%s:%d unhandled header: %s\n",__FILE__,__LINE__,line);
		}
	} else if (!strncasecmp("host:",line,5)) {
		// ignore host: hostname
	} else if (!strncasecmp("accept:",line,7)) {
		// ignore accept: */*
	} else if (!strncasecmp("user-agent:",line,11)) {
//		fprintf(stderr,"%s:%d user-agent: \"%s\"\n",__FILE__,__LINE__,line+11);
	} else if (!strncasecmp("soapaction:",line,11)) {
		char *temp;
		for (temp=line+11;isspace(*temp);temp++);
		strncpy(request->soapaction,temp,MAX_SOAPACTION_REQUEST);
	} else if (!strncasecmp("range:",line,6)) {
		request->isrange=1;
		if (parserange(request,line+6)) GOTOERROR;
	} else if (!strncasecmp("connection:",line,11)) {
//		fprintf(stderr,"%s:%d connection: \"%s\"\n",__FILE__,__LINE__,line+11);
	} else if (!strncasecmp("accept-encoding:",line,16)) {
//		fprintf(stderr,"%s:%d accept-encoding: \"%s\"\n",__FILE__,__LINE__,line+16);
	} else {
		log_shared(shared,1,"%s:%d unhandled header: %s\n",__FILE__,__LINE__,line);
	}
}
return 0;
error:
	*istimeout_errorout=istimeouterror;
	return -1;
}

int makereply(struct shared *shared, struct request *request, struct replybuffer *replybuffer) {

replybuffer->isrange=request->isrange;
replybuffer->range.start=request->rangestart;
replybuffer->range.limit=request->rangelimit;

switch (request->fileindex) {
	case ROOTXML_FILEINDEX_REQUEST:
		if (make_rootxml(shared,replybuffer)) GOTOERROR;
		replybuffer->fullsize=replybuffer->bufflen-replybuffer->internal.left;
		break;
	case ICONPNG_FILEINDEX_REQUEST:
		if (addraw_replybuffer(replybuffer,iconpng_global,size_iconpng_global,"image/png")) GOTOERROR;
		replybuffer->fullsize=replybuffer->bufflen-replybuffer->internal.left;
		break;
	case CONTENTDIR_FILEINDEX_REQUEST:
		if (handle_browse(shared,request,replybuffer)) GOTOERROR;
		replybuffer->fullsize=replybuffer->bufflen-replybuffer->internal.left;
		break;
	case ONEFLAC_FILEINDEX_REQUEST:
		if (!request->file) {
			(void)add404_replybuffer(replybuffer);
		} else {
			if (addfile_replybuffer(replybuffer,request->file->filename,"audio/x-flac",
					request->isrange,request->rangestart,request->rangelimit)) GOTOERROR;
		}
		break;
	case ONEWAV_FILEINDEX_REQUEST:
		if (!request->file) {
			(void)add404_replybuffer(replybuffer);
		} else {
			if (addfile_replybuffer(replybuffer,request->file->filename,"audio/x-wav",
					request->isrange,request->rangestart,request->rangelimit)) GOTOERROR;
		}
		break;
	case ONEMP3_FILEINDEX_REQUEST:
		if (!request->file) {
			(void)add404_replybuffer(replybuffer);
		} else {
			if (addfile_replybuffer(replybuffer,request->file->filename,"audio/mpeg",
					request->isrange,request->rangestart,request->rangelimit)) GOTOERROR;
		}
		break;
	case ONEMP4_FILEINDEX_REQUEST:
		if (!request->file) {
			(void)add404_replybuffer(replybuffer);
		} else {
			if (addfile_replybuffer(replybuffer,request->file->filename,"video/mp4",
					request->isrange,request->rangestart,request->rangelimit)) GOTOERROR;
		}
		break;
	case MERGE_FILEINDEX_REQUEST:
		if (addmerge_replybuffer(shared,replybuffer,request->isrange,request->rangestart,request->rangelimit)) GOTOERROR;
		replybuffer->fullsize=replybuffer->bufflen-replybuffer->internal.left;
		break;
	default:
		(void)add404_replybuffer(replybuffer);
		break;
}

if (replybuffer->iserror) {
	(void)add500_replybuffer(replybuffer);
}
return 0;
error:
	return -1;
}

static int offsetfilename_readn(char *filename, uint64_t offset, unsigned char *dest, unsigned int n) {
int fd=-1;

if (0>(fd=open(filename,O_RDONLY))) GOTOERROR;
if (offset) {
	if (0>lseek(fd,offset,SEEK_SET)) GOTOERROR;
}
if (readn(fd,dest,n)) GOTOERROR;
(ignore)close(fd);
return 0;
error:
	ifclose(fd);
	return -1;
}

static int merge_readn(struct shared *shared, uint64_t offset, unsigned char *dest, unsigned int destlen) {
unsigned int idx,idxlimit,left;

idxlimit=shared->max_files;
if (!idxlimit) GOTOERROR;
idx=0;
left=destlen;
while (1) {
	struct file_shared *file;
	unsigned int n,fileremains;

	file=shared->files[idx];
	if (file->size<=offset) {
		offset-=file->size;
		idx++;
		if (idx==idxlimit) GOTOERROR;
		continue;
	}
	n=left;
	fileremains=file->size-offset;
	if (n>fileremains) n=fileremains;
	if (offsetfilename_readn(file->filename,offset,dest,n)) GOTOERROR;

	left-=n;
	if (!left) break;
	idx++;
	dest+=n;
	offset=0;
}
return 0;
error:
	return -1;
}

static int sendreply(int *istimeout_errorout, struct shared *shared, struct request *request, struct replybuffer *replybuffer,
		int fd_in) {
char datestr[30];
char *buff;
int len;
int istimeouterror=0;

buff=(char *)shared->buff512;

(void)httpctime_misc(datestr,time(NULL));

if (replybuffer->isrange) {
	if (replybuffer->range.start>replybuffer->fullsize) replybuffer->range.start=replybuffer->fullsize;
	if (!replybuffer->range.limit) replybuffer->range.limit=replybuffer->fullsize;
	else if (replybuffer->range.limit>replybuffer->fullsize) replybuffer->range.limit=replybuffer->fullsize;
}

if (replybuffer->replycode) {
	len=snprintf(buff,512,"HTTP/1.1 %u %s\r\n"\
			"%s"\
			"Connection: close\r\n"\
			"Server: %s DLNADOC/1.50 UPnP/1.0 %s\r\n"\
			"Date: %s\r\n"\
			"EXT:\r\n"\
			"\r\n",
			replybuffer->replycode, replybuffer->replycodemsg,
			replybuffer->contenttype,
			shared->server.machine,shared->server.version,
			datestr);
} else if (replybuffer->isrange) {
	if (replybuffer->range.start==replybuffer->fullsize) {
		len=snprintf(buff,512,"HTTP/1.1 206 Partial Content\r\n"\
				"%s"\
				"Connection: close\r\n"\
				"Content-Length: 0\r\n"\
				"Server: %s DLNADOC/1.50 UPnP/1.0 %s\r\n"\
				"Date: %s\r\n"\
				"EXT:\r\n"\
				"\r\n",
				replybuffer->contenttype,
				shared->server.machine,shared->server.version,
				datestr);
	} else {
			len=snprintf(buff,512,"HTTP/1.1 206 Partial Content\r\n"\
					"%s"\
					"Connection: close\r\n"\
					"Content-Length: %"PRIu64"\r\n"\
					"Content-Range: bytes %"PRIu64"-%"PRIu64"/%"PRIu64"\r\n"\
					"Server: %s DLNADOC/1.50 UPnP/1.0 %s\r\n"\
					"Date: %s\r\n"\
					"EXT:\r\n"\
					"\r\n",
					replybuffer->contenttype,
					replybuffer->range.limit-replybuffer->range.start,
					replybuffer->range.start,replybuffer->range.limit-1,replybuffer->fullsize,
					shared->server.machine,shared->server.version,
					datestr);
	}
} else {
	len=snprintf(buff,512,"HTTP/1.1 200 OK\r\n"\
			"%s"\
			"Connection: close\r\n"\
			"Content-Length: %"PRIu64"\r\n"\
			"Server: %s DLNADOC/1.50 UPnP/1.0 %s\r\n"\
			"Date: %s\r\n"\
			"EXT:\r\n"\
			"\r\n",
			replybuffer->contenttype,
			replybuffer->fullsize,
			shared->server.machine,shared->server.version,
			datestr);
}
if ((len<0)||(len>=512)) GOTOERROR;
#ifdef DEBUG
	if (!(replybuffer->debug.replyheader=strdup(buff))) GOTOERROR;
#endif
if (timeout_writen(&istimeouterror,fd_in,(unsigned char *)buff,len,time(NULL)+10)) {
#ifdef DEBUG
	fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,replybuffer->debug.replyheader);
#endif
	if (istimeouterror) GOTOERROR;
	GOTOERROR;
}
UPACKET_DUMP(buff,len,"reply header");

if (replybuffer->external.fd>=0) {
	uint64_t left;
	if (replybuffer->isrange) {
		if (replybuffer->range.start) {
			if (0>lseek(replybuffer->external.fd,replybuffer->range.start,SEEK_SET)) GOTOERROR;
		}
		left=replybuffer->range.limit-replybuffer->range.start;
	} else {
		left=replybuffer->fullsize;
	}
	while (left) {
		uint64_t n;
		n=left;
		if (n>replybuffer->bufflen) n=replybuffer->bufflen;
		if (readn(replybuffer->external.fd,replybuffer->buff,n)) GOTOERROR;

		if (timeout_writen(&istimeouterror,fd_in,replybuffer->buff,n,time(NULL)+30)) {
			if (istimeouterror) GOTOERROR;
			GOTOERROR;
		}
#ifdef DEBUG
		replybuffer->debug.byteswritten+=n;
#endif
		left-=n;
	}
	upacket_dump(NULL,replybuffer->fullsize-replybuffer->offset,0,"reply",__FILE__,__LINE__);
} else if (request->fileindex==MERGE_FILEINDEX_REQUEST) {
	uint64_t left,offset;

	if (replybuffer->isrange) {
		offset=replybuffer->range.start;
		left=replybuffer->range.limit-replybuffer->range.start;
	} else {
		left=replybuffer->fullsize;
		offset=0;
	}
	while (left) {
		uint64_t n;
		n=left;
		if (n>replybuffer->bufflen) n=replybuffer->bufflen;
		if (merge_readn(shared,offset,replybuffer->buff,n)) GOTOERROR;

		if (timeout_writen(&istimeouterror,fd_in,replybuffer->buff,n,time(NULL)+30)) {
			if (istimeouterror) GOTOERROR;
			GOTOERROR;
		}
#ifdef DEBUG
		replybuffer->debug.byteswritten+=n;
#endif
		left-=n;
		offset+=n;
	}
} else {
	if (replybuffer->isrange) {
		if (timeout_writen(&istimeouterror,fd_in,replybuffer->buff+replybuffer->range.start,
				replybuffer->range.limit-replybuffer->range.start,time(NULL)+30)) {
			if (istimeouterror) GOTOERROR;
			GOTOERROR;
		}
#ifdef DEBUG
		replybuffer->debug.byteswritten+=replybuffer->range.limit-replybuffer->range.start;
#endif
	} else {
		if (timeout_writen(&istimeouterror,fd_in,replybuffer->buff,replybuffer->fullsize,time(NULL)+30)) {
			if (istimeouterror) GOTOERROR;
			GOTOERROR;
		}
#ifdef DEBUG
		replybuffer->debug.byteswritten+=replybuffer->fullsize;
#endif
	}
	upacket_dump(replybuffer->buff,responsesize,0,"reply",__FILE__,__LINE__);
}
#ifdef DEBUG
fprintf(stderr,"%s:%d reply finished pid:%d, %"PRIu64" bytes sent\n",__FILE__,__LINE__,getpid(),replybuffer->debug.byteswritten);
#endif
return 0;
error:
	*istimeout_errorout=istimeouterror;
	return -1;
}

static int child_handleclient(int *istimeout_errorout, struct shared *shared, int fd_in) {
struct replybuffer replybuffer;
struct request request;
struct lineio lineio;
time_t expires;
int istimeouterror=0;

clear_replybuffer(&replybuffer);
clear_request(&request);
clear_lineio(&lineio);

if (init_replybuffer(&replybuffer,1024*1024)) GOTOERROR; // larger than POST, larger than internal replies, not too large to timeout, too small means more io calls
voidinit_lineio(&lineio,shared->buff512,512);

expires=time(NULL)+30;

if (getrequest(&istimeouterror,shared,&request,fd_in,&lineio,expires)) {
	if (istimeouterror) GOTOERROR;
	GOTOERROR;
}
if (request.postlen) {
	if (request.postlen>=replybuffer.bufflen) GOTOERROR; // reserve 1 for 0
	if (getpost_lineio(&istimeouterror,&lineio,fd_in,expires,replybuffer.buff,request.postlen)) {
		if (istimeouterror) GOTOERROR;
		GOTOERROR;
	}
	replybuffer.buff[request.postlen]=0;
#ifdef DEBUG
	if (!(request.debug.post=malloc(request.postlen+1))) GOTOERROR;
	memcpy(request.debug.post,replybuffer.buff,request.postlen+1);
#endif
}

#if 0
#warning shorting flacs
if (request.fileindex&ONEFLAC_FILEINDEX_REQUEST) {
	fprintf(stderr,"%s:%d shorting response\n",__FILE__,__LINE__);
	replybuffer.debug.isshortreply=1;
}
#endif
if (makereply(shared,&request,&replybuffer)) GOTOERROR;

if (sendreply(&istimeouterror,shared,&request,&replybuffer,fd_in)) {
	if (istimeouterror) goto error;
	GOTOERROR;
}

// deinit_request(&request);
// deinit_replybuffer(&replybuffer);
return 0;
error:
	// deinit_request(&request);
	// deinit_replybuffer(&replybuffer);
	*istimeout_errorout=istimeouterror;
	return -1;
}

static void cancelchild(struct shared *shared, pid_t pid) {
unsigned int ui;
for (ui=0;ui<shared->children.max;ui++) {
	struct onechild_shared *ocs;
	ocs=&shared->children.list[ui];
	if (ocs->isrunning && (ocs->pid==pid)) {
#if 0
		fprintf(stderr,"%s:%d removing child pid:%d\n",__FILE__,__LINE__,pid);
#endif
		ocs->isrunning=0;
		shared->children.count-=1;
		return;
	}
}
#ifdef DEBUG
fprintf(stderr,"%s:%d no child found? (%d)\n",__FILE__,__LINE__,pid);
#endif
}
static void addchild(struct shared *shared, pid_t pid) {
// check to make sure we're not full before calling
unsigned int ui;
for (ui=0;ui<shared->children.max;ui++) {
	struct onechild_shared *ocs;
	ocs=&shared->children.list[ui];
	if (!ocs->isrunning) {
		ocs->isrunning=1;
		ocs->pid=pid;
		shared->children.count+=1;
		break;
	}
}
}

void reap_httpd(struct shared *shared) {
while (1) {
	pid_t r;
	r=waitpid(-1,NULL,WNOHANG);
	if (r<=0) break;
	(void)cancelchild(shared,r);
}
}

int acceptclient_httpd(struct shared *shared) {
struct sockaddr_in sa;
socklen_t ssa;
int fd=-1;
pid_t pid;

if (shared->children.count==shared->children.max) GOTOERROR; // misconfig

ssa=sizeof(sa);
fd=accept(shared->tcp_socket,(struct sockaddr*)&sa,&ssa);
if (0>fd) return 0;
if (ssa!=sizeof(sa)) GOTOERROR;

uint32_t u32=sa.sin_addr.s_addr;
if (shared->target.ipv4 && (shared->target.ipv4!=u32)) {
	log_shared(shared,1,"%s:%d rejecting connection from %u.%u.%u.%u\n",__FILE__,__LINE__,
			(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff);
	close(fd);
	return 0;
}
log_shared(shared,1,"%s:%d got connection from %u.%u.%u.%u\n",__FILE__,__LINE__,
		(u32)&0xff, (u32>>8)&0xff, (u32>>16)&0xff, (u32>>24)&0xff);

pid=fork();
if (!pid) {
	int istimeout;
	(void)afterfork_shared(shared);
	if (child_handleclient(&istimeout,shared,fd)) {
		if (istimeout) {
//			fprintf(stderr,"%s:%d client timed out\n",__FILE__,__LINE__);
		}
	}
//	close(fd);
	_exit(0);
}
close(fd);fd=-1;
#if 0
	fprintf(stderr,"%s:%d starting child pid:%d\n",__FILE__,__LINE__,pid);
#endif
(void)addchild(shared,pid);

(void)reap_httpd(shared);
#ifdef DEBUG
if (shared->children.max==shared->children.count) {
	fprintf(stderr,"%s:%d maximum children are running, blocking for waitpid\n",__FILE__,__LINE__);
}
#endif
while (shared->children.max==shared->children.count) {
	pid_t r;
	r=waitpid(-1,NULL,0);
	if (r<=0) {
		if (!r) continue;
		if (errno==EINTR) continue;
		GOTOERROR;
	}
	(void)cancelchild(shared,r);
}

return 0;
error:
	ifclose(fd);
	return -1;
}

#if 0
int mainloop_httpd(struct shared *shared, unsigned int seconds) {
struct pollfd pollfd;
int r;
pollfd.fd=shared->tcp_socket;
pollfd.events=POLLIN;

r=poll(&pollfd,1,1000*seconds);
if (0>r) {
	if (errno!=EINTR) GOTOERROR;
} else if (r) {
	if (pollfd.revents&POLLIN) {
		if (acceptclient_httpd(shared)) GOTOERROR;
	}
}
return 0;
error:
	return -1;
}
#endif

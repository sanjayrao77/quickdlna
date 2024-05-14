/*
 * main.c - a simple dlna server for audio and video
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
#include <signal.h>
#include <poll.h>
// #define DEBUG
#include "common/conventions.h"
#include "common/blockmem.h"
#include "interfaces.h"
#include "shared.h"
#include "ssdp.h"
#include "files.h"
#include "httpd.h"
#include "options.h"

static int isquit_global;

static void quit_signal_handler(int ign) {
isquit_global=1;
}
static void chld_signal_handler(int ign) {
}

static int step_mainloop(struct shared *shared, unsigned int seconds) {
struct pollfd pollfds[2];
int r;
int numpfds=1;

pollfds[0].fd=shared->tcp_socket;
pollfds[0].events=POLLIN;

if (!shared->options.isnodiscovery) {
	numpfds=2;
	pollfds[1].fd=shared->udp_socket;
	pollfds[1].events=POLLIN;
}

r=poll(pollfds,numpfds,seconds*1000);
if (r<0) {
	if (errno!=EINTR) GOTOERROR;
	(void)reap_httpd(shared);
} else if (r) {
	if (pollfds[0].revents&POLLIN) {
		if (acceptclient_httpd(shared)) GOTOERROR;
	}
	if (pollfds[1].revents&POLLIN) {
		if (checkclient_ssdp(shared)) GOTOERROR;
	}
}

return 0;
error:
	return -1;
}

int main(int argc, char **argv) {
struct interfaces interfaces;
struct shared shared;
time_t nextalive=0;

clear_interfaces(&interfaces);
clear_shared(&shared);

if (argc<2) {
	printusage_options();
	return 0;
}

if (init_shared(&shared)) GOTOERROR;
if (init_options(&shared,argc,argv)) GOTOERROR;
if (shared.options.ismergefiles) {
	log_shared(&shared,1,"%s:%d --mergefiles is enabled.\n"\
		". This treats every file as a flac file and concats them into one file.\n"\
		". This is meant to overcome players that can't play multiple files correctly.\n",__FILE__,__LINE__);
}
if (shared.isquit) _exit(0);
if (shared.options.isnodiscovery && shared.options.isnoadvertising) {
	log_shared(&shared,1,"%s:%d warning: both SSDP discovery and advertising are disabled.\n",__FILE__,__LINE__);
}
if (allocs_shared(&shared)) GOTOERROR;
if (init_interfaces(&interfaces)) GOTOERROR;
if (init_files(&shared)) GOTOERROR;
{
	uint32_t u32;
	if (getipv4multicastip_interfaces(&u32,&interfaces)) GOTOERROR;
	if (!u32) GOTOERROR;
	shared.ipv4_interface=u32;
	(void)setuuid_shared(&shared);
	deinit_interfaces(&interfaces);
	clear_interfaces(&interfaces);
}
if (getsocket_ssdp(&shared)) GOTOERROR;
if (getsocket_httpd(&shared)) GOTOERROR;

if (shared.options.isbackground) {
	pid_t pid;
	pid=fork();
	if (pid<0) GOTOERROR;
	if (pid) _exit(0);
}

(ignore)signal(SIGTERM,quit_signal_handler);
(ignore)signal(SIGINT,quit_signal_handler);
(ignore)signal(SIGCHLD,chld_signal_handler);
(ignore)signal(SIGPIPE,SIG_IGN);

while (!isquit_global && !shared.isquit) {
	time_t now;

	now=time(NULL);
	if (nextalive<=now) {
		if (!shared.options.isnoadvertising) {
#ifdef DEBUG
			log_shared(&shared,1,"%s:%d sending alives\n",__FILE__,__LINE__);
#endif
			if (alives_send_ssdp(&shared,"900")) GOTOERROR; // 15 minute expiration
		}
		nextalive=now+60*5; // don't try again for at least 5 minutes
	}
		
	if (step_mainloop(&shared,60*5)) GOTOERROR;
}

if (!shared.options.isnoadvertising) {
	log_shared(&shared,1,"%s:%d sending byebye messages\n",__FILE__,__LINE__);
	if (byebyes_send_ssdp(&shared)) GOTOERROR;
	usleep(100*1000);
	if (byebyes_send_ssdp(&shared)) GOTOERROR;
}

deinit_shared(&shared);
// deinit_interfaces(&interfaces);
return 0;
error:
	deinit_shared(&shared);
	deinit_interfaces(&interfaces);
	return -1;
}

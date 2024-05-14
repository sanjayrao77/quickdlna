# quickdlna, a simple way to share a file or directory of music and video to Roku Media Player and other DLNA players, written in C

## Overview

Quickdlna can be used with Roku devices to send music or video to their Roku Media Player.
It might work with other DLNA players as well.

I mainly use it to share flac music files but it can share wav, mp3 and mp4 files to some
extent.

Rokuremote is written for unix-like systems, mainly linux. It uses no external
libraries and doesn't require root permissions.

## Installing

After downloading the source with "git clone https://github.com/sanjayrao77/quickdlna"
you should be able to build it with a simple "make" on linux.

This could be fairly easily modified to run on non-linux systems. The only tricky part
is the UDP multicast, which is not essential anyway.

## Security

There's a "targetip=xxx.xxx.xxx.xxx" command line option to restrict requests from a given
ip. Without that option, anyone on the network can read the exported files.

The number of forked processes is limited.

## Command line arguments

```
Usage: quickdlna [FILE]..[FILE] [option]..[option] [flag]..[flag]
Options:
   instance=INT     : allows multiple copies, given different values
   children=INT     : allow this many simultaneous requests
   targetip=IPV4    : instead of multicast, send only to the given IP
   name=STRING      : use XX as the server name
   machine=STRING   : use XX as the server type
   version=STRING   : use XX as the server version
Flags:
   --help           : this
   --nodiscovery    : don't listen for M-SEARCH broadcasts
   --forcediscovery : exit if we can't bind port 1900
   --noadvertising  : don't broadcast alive and byebye SSDP
   --quiet          : print no messages
   --verbose        : print extra messages
   --syslog         : print messages to syslog
   --background     : run in background, enables --syslog
   --mergefiles     : merge all files into one, should be flacs
```

### Quick start

When run with filenames but no options, quickdlna should basically just work. It will
advertise itself to players with SSDP and respond to SSDP discovery requests.

The easiest way to try it is to run "quickdlna \*.flac" and see if you can play
anything on your player.

### instance=INT

If you want to run multiple simultaneous copies, you'll need to use this option to distinguish them.

You could start the second instance with "instance=2" and so on.

This is needed so players can distinguish the different servers.

### children=INT

Quickdlna will fork to respond to http requests. By default, it's limited to 5 children. When it is streaming
5 simultaneous files, it will stop responding to new requests until one finishes.

The default is pretty good. You should increase it if you have multiple players. Some players can have 2 simultaneous
requests, so you could set children=N, where N is twice the number of players plus 1, having 1 for configuration requests.

You might want to reduce it if you have very little ram.

Example: "children=3".

### targetip=IPV4

By default, quickdlna will broadcast to the subnet and accept requests from anything that can reach it. This is normal
for DLNA and is how all the other servers I've seen work.

If you want to limit quickdlna to a single player, you can use this option.

Once set, quickdlna will **not** broadcast to the subnet. Instead, it will broadcast to the given IP address. Also, it will
not reply to any client unless they have the given IP address.

This is useful if you don't want to spam all your other players with this source. Also, it could be useful if you share your
subnet with other people and you don't want them to mooch off your server. Or, it could be useful if you're borrowing someone's
wifi and you don't want to annoy them.

Example: "targetip=192.168.1.231"

The "IPV4" value is passed to inet\_addr(), but generally should be an IP version 4 address, in the form "xxx.xxx.xxx.xxx".

### name=STRING, machine=STRING, version=STRING

These options are pretty useless. They're sent in the network communication but I'm not sure if anything reads it.

These options are here in case anyone wants to advertise something specific or to spoof another server.

### --nodiscovery

This will disable listening for discovery requests. As a consequence, there's no
need to bind to UDP port 1900. If there's another dlna service running on your system,
you may want to use this flag to reserve port 1900 for the other service.

### --forcediscovery

Normally, if port 1900 is already in use, quickdlna will continue without discovery
enabled. If you'd rather it require this feature, you can use "--forcediscovery".
With this flag enabled, quickdlna will keep trying to bind port 1900 and exit if
it's unsuccessful.

### --noadvertising

This flag disables SSDP advertising. Without this flag, quickdlna will send packets
announcing its presence.

This works independently of discovery. If you disable both discovery and advertising,
players won't be able to find quickdlna automatically. But, if you don't need automatic
configuration, --nodiscovery and --noadvertising will disable UDP network spamming.

### --quiet

This reduces printed messages.

### --verbose

This increases printed messages.

### --syslog

This prints messages to syslog instead of stderr.

### --background

This runs quickdlna in the background. It enables --syslog at the same time.

### --mergefiles

This combines all the files provided into one merged file. I use this for combining an album
of flac files into a single flac file. This is good for players that don't walk playlists well.

## Usage

After running quickdlna, try running the "Roku Media Player" app on a Roku device. If the app starts for the first time,
it should present four options. Try selecting "All," the first option. If the player has found quickdlna correctly,
there should now be a "Quick" icon visible. If you don't see it, try restarting quickdlna and making sure they're on the
same subnet.

After selecting the "Quick" icon, you should see your media files right there. You can click on one to play it.

A lot of the file attributes (like duration, artist, etc.) are presented incorrectly, especially for wav, mp3 and mp4. My focus
has been for flac.

## Bugs and compatibility

I've only tested this on a Roku TV, Roku Express and Roku Express 4k, using their "Roku Media Player" app.

I mainly use FLAC files, so they're supported the best. MP3, WAV and
MP4 files should work but a lot of the file attributes will be presented incorrectly.

Quickdlna does **no** transcoding so if the player doesn't support the native file format, it's
not going to work. It's worked for the few MP3, WAV and MP4 files that I've tried but I have no idea what the
general compatibility is.

## Roku Express and Express 4k bug

There seems to be a bug on my Roku Express and Express 4k when playing a full album. My Roku TV works fine but
the Express will play 2 or 3 flac files and then stop.

To deal with this, I have a --mergefiles
option which combines an album into a single file so the player doesn't stop. Thankfully, the Roku player
will play concatenated flac files without needing to decompress them.

Another way to get around this bug is to enable a 1 minute screen saver on the Roku Express. For whatever reason,
the media player will keep playing while the screen saver is running.

## Optional dependencies

I use inkscape to create the graphical icon displayed in the player. If you have inkscape installed,
you can modify icon.svg then run "make" to update the icon.

Older versions of inkscape have used different command line parameters, so you'd need to update the Makefile
if you have one of those.

## Acknowledgements

Many thanks to minidlna. I reverse engineered the DLNA protocol by looking at minidlna's packets.

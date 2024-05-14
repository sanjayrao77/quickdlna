
#define MUSICMASK_TYPE_FILE_SHARED	7
#define FLAC_TYPE_FILE_SHARED	1
#define WAV_TYPE_FILE_SHARED	2
#define MP3_TYPE_FILE_SHARED	3
#define VIDEO_TYPE_FILE_SHARED	8

struct file_shared {
	uint64_t size;
	char *filename;
	int type;
};

struct shared {
	uint32_t ipv4_interface;
	int udp_socket; // for ssdp
	unsigned short tcp_port;
	int tcp_socket; // for http
	unsigned char *buff512;
	struct {
#define LEN_UUID_SHARED	36
		char uuid[LEN_UUID_SHARED+1];
#define MAX_MACHINE_SHARED 15
		char machine[MAX_MACHINE_SHARED+1];
#define MAX_VERSION_SHARED 15
		char version[MAX_VERSION_SHARED+1];
#define MAX_FRIENDLY_SHARED	15
		char friendly[MAX_FRIENDLY_SHARED+1];
		uint32_t instance; // index for multiple simultaneous copies
	} server;
	unsigned int max_files;
	struct file_shared **files;
	struct {
		uint32_t ipv4;
	} target;
	struct {
		unsigned int count,max;
		struct onechild_shared {
			int isrunning;
			pid_t pid;
		} *list;
	} children;
	struct {
		int isnodiscovery; // => don't bind udp on 1900; don't respond to m-search
		int isnoadvertising; // => don't send alives or byebyes
		int isquiet;
		int isverbose;
		int issyslog;
		int isbackground;
		int isforcediscovery;
		int ismergefiles;
	} options;
	int isquit;
	struct blockmem blockmem;
};
H_CLEARFUNC(shared);

void deinit_shared(struct shared *s);
int init_shared(struct shared *s);
void afterfork_shared(struct shared *s);
void setuuid_shared(struct shared *s);
int allocs_shared(struct shared *s);
void log_shared(struct shared *shared, int verbose, char *format, ...);

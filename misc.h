
unsigned int slowtou(char *str);
uint64_t slowtou64(char *str);
int parseipv4_utils(uint32_t *ipv4_out, char *str);
int readn(int fd, unsigned char *msg, unsigned int len);
int writen(int fd, unsigned char *msg, unsigned int len);
int timeout_readn(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires);
int timeout_writen(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires);
int timeout_readpacket(int *istimeout_errorout, int fd, unsigned char *msg, unsigned int len, time_t expires);
void httpctime_misc(char *dest, time_t t);

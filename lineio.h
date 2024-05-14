
struct lineio {
	unsigned char *buff;
	unsigned int bufflen;

	unsigned char *cursor;
	unsigned int unreadcount; // cursor+unreadcount+towritecount==(buff+bufflen)
	unsigned int towritecount;
};
H_CLEARFUNC(lineio);

void voidinit_lineio(struct lineio *lineio, unsigned char *buffer, unsigned int bufferlen);
void reset_lineio(struct lineio *lineio);
unsigned char *gets_lineio(int *istimeout_errorout, unsigned int *len_out, struct lineio *lineio, int fd, time_t expires);
int getpost_lineio(int *istimeout_errorout, struct lineio *lineio, int fd, time_t expires, unsigned char *dest, unsigned int destlen);

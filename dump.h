#ifdef DUMP
int upacket_dump(unsigned char *data, uint64_t len, unsigned int towrite, char *tag, char *file, unsigned int line);
int string_dump(char *file, unsigned int line, char *format, ...);
#define PACKET_DUMP(a,b) upacket_dump((unsigned char *)a,strlen(a),strlen(a),b,__FILE__,__LINE__)
#define UPACKET_DUMP(a,b,c) upacket_dump((unsigned char *)a,b,b,c,__FILE__,__LINE__)
#else
#define upacket_dump(a,b,c,d,e,f) do { } while (0)
#define string_dump(a,b,c,d,e,f) do { } while (0)
#define PACKET_DUMP(a,b) do { } while (0)
#define UPACKET_DUMP(a,b,c) do { } while (0)
#endif

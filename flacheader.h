
#define MAXSTR_FLACHEADER	256
struct flacheader {
	char title[MAXSTR_FLACHEADER+1];
	char artist[MAXSTR_FLACHEADER+1];
	char album[MAXSTR_FLACHEADER+1];
	char date[MAXSTR_FLACHEADER+1];
	int duration; /* in seconds, -1 => dunno */
	unsigned int tracknumber;

	unsigned int samplerate;
	unsigned int high_samplecount;
	unsigned int low_samplecount;
};

int read_flacheader(struct flacheader *dest, char *filename);

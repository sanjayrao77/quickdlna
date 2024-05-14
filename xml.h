
struct string_xml {
	unsigned char *ustr;
	unsigned int len;
};

struct attribute_xml {
	struct attribute_xml *next;
	struct string_xml name,value;
};

struct tag_xml {
	struct tag_xml *parent;
	struct string_xml name;
	struct string_xml value;

	struct tag_xml *first_child;
	struct tag_xml *next_sibling;

	struct attribute_xml *first_attribute;
};

struct xml {
	struct tag_xml top;
	struct tag_xml *filling_tag;
	unsigned int subfillingdepth; // how deep are we below filling, 0=> in filling
};
H_CLEARFUNC(xml);

void removecomments_xml(unsigned *datalen_inout, unsigned char *data);
int parse_xml(struct xml *xml, unsigned char *data, unsigned int datalen);
void set_tag_xml(struct tag_xml *tag, struct tag_xml *parent, char *namestr);
void set_attribute_xml(struct tag_xml *tag, struct attribute_xml *attribute, char *namestr);

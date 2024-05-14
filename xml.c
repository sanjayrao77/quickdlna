/*
 * xml.c - a simple xml parser with no dynamic memory allocation
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
// #define DEBUG
#include "common/conventions.h"

#include "xml.h"

CLEARFUNC(xml);

void removecomments_xml(unsigned *datalen_inout, unsigned char *data) {
unsigned int datalen=*datalen_inout;
unsigned int lenleft;
unsigned char *cursor;

cursor=data;
lenleft=datalen;
while (1) {
	if (lenleft<4) break;
	if (memcmp(cursor,"<!--",4)) {
		cursor+=1;
		lenleft-=1;
		continue;
	}
	unsigned char *c2;
	unsigned int ll2;
	c2=cursor+4;
	ll2=lenleft-4;
	while (1) {
		if (ll2<3) {
//			datalen-=lenleft; // this would treat unclosed comment "<!-- .." as a comment
			goto doublebreak;
		}
		if (memcmp(c2,"-->",3)) {
			c2++;
			ll2--;
			continue;
		}
		c2+=3;
		ll2-=3;
		memmove(cursor,c2,ll2);
		datalen-=(unsigned int)(c2-cursor);
		cursor=c2;
		lenleft=ll2;
		break;
	}
}
doublebreak:
*datalen_inout=datalen;
}

static int getstart_parse_xml(unsigned int *startlen_out, unsigned char *data, unsigned int lenleft) {
unsigned char *cursor=data;
while (1) {
	if (!lenleft) break;
	if (*cursor!='<') {
		cursor+=1;
		lenleft-=1;
		continue;
	}
	if (lenleft<5) break;
	if (memcmp(cursor,"<?xml",5)) break;
	cursor+=5;
	lenleft-=5;
	while (1) {
		if (lenleft<2) GOTOERROR;
		if (memcmp(cursor,"?>",2)) {
			cursor+=1;
			lenleft-=1;
			continue;
		}
		cursor+=2;
		while (1) {
			if (!lenleft) break;
			if (*cursor!='<') {
				cursor+=1;
				lenleft-=1;
				continue;
			}
			break;
		}
		break;
	}
	break;
}
*startlen_out=(unsigned int)(cursor-data);
return 0;
error:
	return -1;
}

static int getgtpos_parse(unsigned int *gtpos_out, unsigned char *data, unsigned int lenleft) {
unsigned char *cursor;
cursor=data;
while (1) {
	if (!lenleft) GOTOERROR;
	if (*cursor!='>') {
		cursor++;
		lenleft--;
		continue;
	}
	break;
}
*gtpos_out=(unsigned int)(cursor-data);
return 0;
error:
	return -1;
}
static int getltpos_parse(unsigned int *ltpos_out, unsigned char *data, unsigned int lenleft_in) {
unsigned char *cursor;
unsigned int lenleft=lenleft_in;
cursor=data;
while (1) {
	if (!lenleft) goto error;
	if (*cursor!='<') {
		cursor++;
		lenleft--;
		continue;
	}
	break;
}
*ltpos_out=(unsigned int)(cursor-data);
return 0;
error:
	return -1;
}

static int getfirstword_parse(unsigned int *wordleadin_out, struct string_xml *word, unsigned char *data, unsigned int lenleft) {
unsigned char *cursor,*wordstart;
unsigned int leadin=0;
cursor=data;
while (1) {
	if (!lenleft) GOTOERROR;
	if (isspace(*cursor)) {
		cursor++;
		lenleft--;
		continue;
	}
	leadin=(unsigned int)(cursor-data);
	break;
}
wordstart=cursor;
if (*cursor=='/') {
	cursor++;	
} else if (*cursor=='>') {
	GOTOERROR;
} else {
	cursor++;
	lenleft--;
	while (1) {
		if (!lenleft) break;
		if (isspace(*cursor)) break;
		if ((*cursor=='/')||(*cursor=='>')) {
			break;
		}
		cursor++;
		lenleft--;
	}
}
*wordleadin_out=leadin;
word->ustr=wordstart;
word->len=(unsigned int)(cursor-wordstart);
return 0;
error:
	return -1;
}

static int gettagopen_parse(unsigned int *openlen_out, int *isclose_out, struct string_xml *dest,
		unsigned char *data, unsigned int lenleft) {
unsigned char *cursor;
unsigned int wordleadin;
struct string_xml word;

// assumes we start with <
cursor=data+1;
lenleft--;
if (getfirstword_parse(&wordleadin,&word,cursor,lenleft)) goto none;
if ((word.len==1)&&(word.ustr[0]=='/')) {
	unsigned int gtpos;
	wordleadin+=1;
	cursor+=wordleadin;
	lenleft-=wordleadin;
	if (getfirstword_parse(&wordleadin,&word,cursor,lenleft)) GOTOERROR;
	wordleadin+=word.len;
	cursor+=wordleadin;
	lenleft-=wordleadin;
	if (getgtpos_parse(&gtpos,cursor,lenleft)) GOTOERROR;
	cursor+=gtpos+1;
	*openlen_out=(unsigned int)(cursor-data);
	*isclose_out=1;
	*dest=word;
	return 0;
}
*openlen_out=word.len+(unsigned int)(word.ustr-data);
*isclose_out=0;
*dest=word;
return 0;
none:
	*openlen_out=0;
	*isclose_out=0;
	dest->ustr=NULL;
	dest->len=0;
	return 0;
error:
	return -1;
}

static int gettagbit_parse(unsigned int *full_out, struct string_xml *bit, unsigned char *data, unsigned int lenleft) {
unsigned char *cursor,*bitstart;
cursor=data;
while (1) {
	if (!lenleft) GOTOERROR;
	if (isspace(*cursor)) {
		cursor++;
		lenleft--;
		continue;
	}
	break;
}
bitstart=cursor;
if ((*cursor=='>')||(*cursor=='/')||(*cursor=='=')) {
	cursor+=1;
} else if (*cursor=='\"') {
	cursor++;
	lenleft--;
	while (1) {
		if (!lenleft) GOTOERROR;
		if (*cursor!='\"') {
			cursor++;
			lenleft--;
			continue;
		}
		cursor++;
//				lenleft--;
		break;
	}
} else {
	cursor+=1;
	lenleft--;
	while (1) {
		if (!lenleft) GOTOERROR;
		if (isspace(*cursor)) break;
		if ((*cursor=='>')||(*cursor=='/')||(*cursor=='=')) break;
		cursor++;
		lenleft--;
	}
}
bit->ustr=bitstart;
bit->len=(unsigned int)(cursor-bitstart);
*full_out=(unsigned int)(cursor-data);
return 0;
error:
	return -1;
}

static unsigned char *trimleftcolon(unsigned int *len_inout, unsigned char *ustr_in) {
unsigned char *ustr=ustr_in;
unsigned int ui;
ui=*len_inout;
while (ui) {
	if (*ustr==':') {
		*len_inout=ui-1;
		return ustr+1;
	}
	ui--;
	ustr++;
}
return ustr_in;
}

static inline struct attribute_xml *findattribute(struct tag_xml *tag, unsigned char *name, unsigned int namelen) {
struct attribute_xml *att;

name=trimleftcolon(&namelen,name);

att=tag->first_attribute;
while (att) {
	if ((att->name.len==namelen)&&(!memcmp(att->name.ustr,name,namelen))) return att;
	att=att->next;
}
return NULL;
}

static inline void setattribute_child(struct tag_xml *tag, struct string_xml *left, struct string_xml *right) {
struct attribute_xml *att;
if (!tag->first_attribute) return;
att=findattribute(tag,left->ustr,left->len);
if (att) {
	struct string_xml r;
	r=*right;
	if (r.len>=2) {
		if ((r.ustr[0]=='\"')&&(r.ustr[r.len-1]=='\"')) {
			r.ustr+=1;
			r.len-=2;
		}
	}
	att->value=r;
}
}

static inline struct tag_xml *findchild(struct tag_xml *tag, unsigned char *name, unsigned int namelen) {
struct tag_xml *child;

name=trimleftcolon(&namelen,name);

#if 0
fprintf(stderr,"%s:%d looking for child \"",__FILE__,__LINE__);
fwrite(name,namelen,1,stderr);
fputs("\"\n",stderr);
fprintf(stderr,"%s:%d looking in \"",__FILE__,__LINE__);
fwrite(tag->name.ustr,tag->name.len,1,stderr);
fputs("\"\n",stderr);
#endif

child=tag->first_child;
while (child) {
#if 0
	fprintf(stderr,"%s:%d checking child \"",__FILE__,__LINE__);
	fwrite(child->name.ustr,child->name.len,1,stderr);
	fputs("\"\n",stderr);
#endif
	if ((child->name.len==namelen)&&(!memcmp(child->name.ustr,name,namelen))) return child;
	child=child->next_sibling;
}
return NULL;
}

static unsigned char *trim(unsigned int *len_out, unsigned char *data, unsigned int lenleft) {
unsigned char *cursor;
cursor=data;
while (1) {
	if (!lenleft) {
		*len_out=0;
		return NULL;
	}
	if (isspace(*cursor)) {
		cursor++;
		lenleft--;
		continue;
	}
	break;
}
unsigned char *end;
for (end=cursor+lenleft-1;isspace(*end);end--);
*len_out=1+(unsigned int)(end-cursor);
return cursor;
}

static int handletag_parse(unsigned int *full_out, struct xml *xml, unsigned char *data, unsigned int lenleft) {
struct string_xml element;
unsigned int ltpos,fullopenlen;
struct tag_xml *child=NULL;
unsigned char *cursor;
int isclosed;

cursor=data;

if (*cursor!='<') {
	unsigned int valuelen;
	if (getltpos_parse(&ltpos,cursor,lenleft)) {
		*full_out=lenleft;
		return 0;
	}
	cursor=trim(&valuelen,cursor,ltpos);
	if (valuelen) {
		if (!xml->subfillingdepth) {
			if (xml->filling_tag->value.len) {
				GOTOERROR;
			}
			xml->filling_tag->value.ustr=cursor;
			xml->filling_tag->value.len=valuelen;
		}
	}
	*full_out=ltpos;
	return 0;
}

if (gettagopen_parse(&fullopenlen,&isclosed,&element,cursor,lenleft)) GOTOERROR;
if (!element.ustr) GOTOERROR;
if (isclosed) {
	if (xml->subfillingdepth) {
		xml->subfillingdepth-=1;
	} else {
		xml->filling_tag=xml->filling_tag->parent;
		if (!xml->filling_tag) GOTOERROR;
	}
	*full_out=fullopenlen;
	return 0;
}

cursor+=fullopenlen;
lenleft-=fullopenlen;
if (xml->subfillingdepth) {
	xml->subfillingdepth+=1;
} else {
	child=findchild(xml->filling_tag,element.ustr,element.len);
	if (!child) {
		xml->subfillingdepth=1;
	} else {
		xml->filling_tag=child;
	}
}
{
	struct string_xml left={.ustr=NULL,.len=0},right={.ustr=NULL,.len=0};
	int equalsstate=0;
	while (1) {
		struct string_xml bit;
		unsigned int fullbitlen;
		if (gettagbit_parse(&fullbitlen,&bit,cursor,lenleft)) GOTOERROR;
		if (!bit.len) GOTOERROR;

#if 0
		fprintf(stderr,"%s:%d bit found (%u): \"",__FILE__,__LINE__,bit.len);
		fwrite(bit.ustr,bit.len,1,stderr);
		fputs("\"\n",stderr);
#endif

		cursor+=fullbitlen;
		lenleft-=fullbitlen;
		if (bit.len==1) {
			if (bit.ustr[0]=='/') {
				isclosed=1;
				continue;
			} else if (bit.ustr[0]=='=') {
				if (equalsstate!=1) {
					left.ustr=NULL; left.len=0;
					equalsstate=2;
				} else equalsstate=2; // ready for right
				continue;
			} else if (bit.ustr[0]=='>') {
				goto fullbreak;
			}
		}
		if (child) {
			if (!equalsstate) { // ready for left
				left=bit;
				equalsstate=1; // waiting for equals
			} else if (equalsstate==2) {
				equalsstate=0; // reset
				right=bit;
#if 0
				fprintf(stderr,"%s:%d setattribute \"",__FILE__,__LINE__);
				if (left.len) fwrite(left.ustr,left.len,1,stderr);
				fputs("\" \"",stderr);
				if (right.len) fwrite(right.ustr,right.len,1,stderr);
				fputs("\"\n",stderr);
#endif
				(void)setattribute_child(child,&left,&right);
			}
		}
	}
fullbreak:
	if (child) {
		if (equalsstate) GOTOERROR;
	}
}

if (isclosed) {
	if (xml->subfillingdepth) {
		xml->subfillingdepth-=1;
	} else {
		xml->filling_tag=xml->filling_tag->parent;
		if (!xml->filling_tag) GOTOERROR;
	}
}

*full_out=(unsigned int)(cursor-data);
return 0;
error:
	return -1;
}

int parse_xml(struct xml *xml, unsigned char *data, unsigned int datalen) {
// this assumes comments have been stripped out already
unsigned int lenleft,startlen;
unsigned char *cursor;

cursor=data;
lenleft=datalen;
if (getstart_parse_xml(&startlen,cursor,lenleft)) GOTOERROR;
// TODO check cursor..startlen for version and encoding
cursor+=startlen;
lenleft-=startlen;

xml->filling_tag=&xml->top;

#if 1
	xml->top.name.ustr=(unsigned char *)"top";
	xml->top.name.len=3;
#endif

while (lenleft) {
	unsigned int full;
	if (handletag_parse(&full,xml,cursor,lenleft)) GOTOERROR;
	if (!full) GOTOERROR; // TODO remove
	cursor+=full;
	lenleft-=full;
}

return 0;
error:
	return -1;
}

void set_tag_xml(struct tag_xml *tag, struct tag_xml *parent, char *namestr) {
tag->parent=parent;
tag->next_sibling=parent->first_child;
parent->first_child=tag;
tag->name.ustr=(unsigned char *)namestr;
tag->name.len=strlen(namestr);
tag->value.ustr=NULL;
tag->value.len=0;
tag->first_child=NULL;
tag->first_attribute=NULL;
}

void set_attribute_xml(struct tag_xml *tag, struct attribute_xml *attribute, char *namestr) {
attribute->name.ustr=(unsigned char *)namestr;
attribute->name.len=strlen(namestr);
attribute->value.ustr=NULL;
attribute->value.len=0;
attribute->next=tag->first_attribute;
tag->first_attribute=attribute;
}


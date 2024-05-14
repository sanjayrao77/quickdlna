CFLAGS=-g -O2 -Wall -D_FILE_OFFSET_BITS=64
all: quickdlna
# CFLAGS=-g -O2 -Wall -D_FILE_OFFSET_BITS=64 -DDUMP -DDEBUG
# all: quickdlna-dump
ICONNAME=Quick

quickdlna: main.o interfaces.o ssdp.o shared.o lineio.o httpd.o misc.o files.o options.o icon.o flacheader.o xml.o common/blockmem.o
	gcc -o $@ $^

quickdlna-dump: main.o interfaces.o ssdp.o shared.o lineio.o httpd.o dump.o misc.o files.o options.o icon.o flacheader.o xml.o common/blockmem.o
	gcc -o $@ $^

icon.png: icon.svg
	cpp -P -DICONNAME=${ICONNAME} icon.svg | inkscape --export-filename icon.png --export-width 120 --export-height 120 --pipe

icon.c: icon.png mkiconc.py
	./mkiconc.py > icon.c

clean:
	rm -f quickdlna quickdlna-dump core *.o common/*.o

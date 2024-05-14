#!/usr/bin/python3 -B

import sys

data=[]
f=open('icon.png','rb')
while True:
	b=f.read(1)
	if b==b'': break
	data.append(b)
f.close()

print("unsigned int size_iconpng_global=%u;"%(len(data)))
print("unsigned char iconpng_global[%u]={"%(len(data)))
linelen=0
while True:
	if not data: break;
	b=data.pop(0)
	if not data:
		str='%s'%b[0]
	else:
		str='%s,'%b[0]
	l=len(str)
	if linelen+l>130:
		print()
		linelen=0
	print(str,end='')
	linelen+=l

print("};")

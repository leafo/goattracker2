all: dat2inc datafile

datafile: datafile.c bme_end.c
	gcc -o datafile datafile.c bme_end.c
	strip datafile

dat2inc: dat2inc.c
	gcc -o dat2inc dat2inc.c
	strip dat2inc


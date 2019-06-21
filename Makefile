CFLAGS=-Wall
LDFLAGS=-lm

all: raw2mpf mpf2raw

raw2mpf: raw2mpf.c verbose.c verbose.h

mpf2raw: mpf2raw.c verbose.c verbose.h

install: 
	cp raw2mpf /usr/local/bin
	cp mpf2raw /usr/local/bin
	cp alsa2mpf /usr/local/bin
	cp mpf2alsa /usr/local/bin

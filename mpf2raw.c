/* mpf2raw.c - convert .mpf files to .raw audio files 8kHz/U8
 *
 * Copyright (C) 2019  Rik Snel <rik@snel.it>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h> // we use assert to test for things that can't happen

#include "verbose.h"

#define MAX_DATA_SIZE 8*1024 // 8kB

/* one period of 1kHz tone U8@8kHz, 1ms */
unsigned char toneO[] = {
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00
};

/* two periods of 2kHz tone U8&8kHz, 1ms */
unsigned char toneX[] = {
	0xff, 0xff, 0x00, 0x00,
	0xff, 0xff, 0x00, 0x00
};

void tone(unsigned char *tone) {
	// 2ms
	fwrite(tone, 1, 8, stdout);
	fwrite(tone, 1, 8, stdout);
}

void bit(int bit) {
	tone(toneX);
	tone(bit?toneO:toneX);
	tone(toneO);
}

void lead_sync() {
	// four seconds 1kHz
	for (int i = 0; i < 2000; i++) tone(toneO);
}

void mid_or_tail_sync() {
	// two seconds 2kHz
	for (int i = 0; i < 1000; i++) tone(toneX);
}

void byte(unsigned char byte) {
	// start bit
	bit(0);

	// the actual bits of the byte
	for (int i = 0; i < 8; i++) {
		bit(byte&0x01);
		byte >>= 1;
	}

	// end bit
	bit(1);
}

void word(unsigned short word) {
	// output the bytes in little endian order
	byte(word&0xff);
	byte(word>>8);
}

unsigned char calc_checksum(unsigned char *buf, unsigned short buf_size) {
	unsigned char checksum = 0;

	for (int i = 0; i < buf_size; i++) checksum += buf[i];

	return checksum;
}

void write_program(unsigned short filename, unsigned short org,
		unsigned char *buf, unsigned short buf_size) {
	lead_sync();

	word(filename);
	word(org);
	word(org + buf_size - 1);
	byte(calc_checksum(buf, buf_size));

	mid_or_tail_sync();

	for (int i = 0; i < buf_size; i++) {
		byte(buf[i]);
	}

	mid_or_tail_sync();
}

int check_format_and_mangle(char *line, int lineno) {
	size_t len;
	char *ptr;

	// format is
	// xxxx/yyyy:zzzzzzzzzz..\n
	//
	// xxxx are four hex digits that represent the 'filename'
	// yyyy are four hex digits that represetn the loading address
	// zzzzzzz are an even number of hex digits that represent the data
	//
	// so the length of the captured string must be ODD (excluding
	// the 0 terminator)
	// 
	// we provide a large buffer for the entire string, if the
	// buffer is completely filled, we assume there is data left
	// and, thus, the data is too large

	len = strlen(line);

	if (len == sizeof(line) - 1) {
		WARNING("line %d of input is too long, skipping", lineno);
		return -1;
	}

	if (len < 13) {
		WARNING("line %d of input is too short, skipping", lineno);
		return -1;
	}

	if (!(len%2)) {
		WARNING("length of input line %d (including \\n) is EVEN, skipping", lineno);
		return -1;
	}

	ptr = line;
	do {
		if (ptr - line == 4) {
			if (*ptr != '/') {
				WARNING("illegal character at position 5 in line %d of input "
						"must be '/', skipping", lineno);
				return -1;
			}
		} else if (ptr - line == 9) {
			if (*ptr != ':') {
				WARNING("illegal character at position 10 in line %d of input "
						"must be ':', skipping", lineno);
				return -1;
			}
		} else if (ptr - line == len - 1) {
			if (*ptr != '\n') {
				WARNING("illegal character at last position in line %d of input "
						"must be '\n', skipping", lineno);
				return -1;

			} else *ptr = 0x10; // use 0x10 as new end marker
		} else if (*ptr >= '0' && *ptr <= '9') {
			*ptr -= '0';
		} else if (*ptr >= 'A' && *ptr <= 'F') {
			*ptr -= 'A' - 10;
		} else if (*ptr >= 'a' && *ptr <= 'f') {
			*ptr -= 'a' - 10;
		} else {
			WARNING("illegal character at position %ld in line %d of input "
					"must be a hex digit", ptr - line , lineno);
			return -1;
		}
	} while (*(++ptr));

	return 0;
}

unsigned char get_byte(char *in) {
	assert(*in < 0x10 && *(in+1) < 0x10);
	return (*in)*16+ *(in+1);
}
			
unsigned short get_word(char *in) {
	return 256*get_byte(in) + get_byte(in + 2);
}

int main(int argc, char *argv[]) {
	// header 10 bytes, data MAX_DATA_SIZE*2 bytes + newline + \0 terminator
	char line[10+MAX_DATA_SIZE*2+1+1], *in;
	unsigned char data[MAX_DATA_SIZE], *out;
	int lineno = 0;
	unsigned short filename, first_addr;

	verbose_init(argv[0]);

	// each line is a program, if there is an error, just emit a WARNING and
	// go to the next line
	while (fgets(line, sizeof(line), stdin)) {
		lineno++;

		if (check_format_and_mangle(line, lineno) == -1) continue;


		filename = get_word(line);
		first_addr = get_word(line+5);
		VERBOSE("found filename=%04x, first_addr=%04x", filename, first_addr);

		in = line + 10; // offset of first databyte
		out = data;
		
		// the first byte of data is valid, since we checked that there
		// is at least one data byte in the file
		assert(*in != 0x10 /* our terminator */);
		do {
			assert(out - data < sizeof(data));
			*(out++) = get_byte(in);
			in += 2;
		} while (*in != 0x10);

		VERBOSE("data length is %ld bytes, generating audio", out - data);
		write_program(filename, first_addr, data, out - data);
	}
}


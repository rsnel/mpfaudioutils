/* raw2mpf.c - convert .raw audio (8kHz/U8) to .mpf 
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
#include <assert.h>
#include <string.h>
#include <endian.h>
#include <math.h>

#include "verbose.h"

/* at 8kHz sampling rate, the duration
 * of a sample is 0.000125ms */
#define DURATION 0.000125 
#define MAX_DATA_SIZE 8*1024 // 8kB

typedef enum state_e { NONE, HEADER, MID_SYNC, FIRST_DATA, DATA, TAIL_SYNC } state_t;

const char *state_string(state_t state) {
	switch (state) {
		case NONE:
			return "NONE";
			break;
		case HEADER:
			return "HEADER";
			break;
		case MID_SYNC:
			return "MID_SYNC";
			break;
		case FIRST_DATA:
			return "FIRST_DATA";
			break;
		case DATA:
			return "DATA";
			break;
		case TAIL_SYNC:
			return "TAIL_SYNC";
			break;
		default:
			assert(0);
	}
}

typedef enum run_e { SHORT, LONG } run_t;

const char *run_string(run_t run) {
	switch (run) {
		case SHORT:
			return "SHORT";
			break;
		case LONG:
			return "LONG";
			break;
		default:
			assert(0);
	}
}

void decode_inner(run_t run, int length) {
	static state_t state = NONE;
	static int bytes, bits, bit, data_size;
	static run_t last_run = SHORT, last_length = 0;
	static unsigned char header[7], data[MAX_DATA_SIZE], checksum;
	static unsigned short first_addr, filename;

	// according to MPF-1 monitor source listing: LEAD_SYNC is
	// accepted if it is longer than 1 second
	// a LEAD_SYNC always resets the state machine
	if (run == LONG && length > 1000) {
		if (state != NONE) fprintf(stderr, "LEAD_SYNC found while in state %s\n", state_string(state));
		bytes = 7; // expecting 7 bytes
		bits = -1; // expecting bit 0 (startbit)
		state = HEADER;
		memset(header, 0, sizeof(header));
		memset(data, 0, sizeof(data));
		VERBOSE("found LEAD_SYNC, loading HEADER");
	} else switch (state) {
		case NONE:
			// if we are in state NONE and there is no LEAD_SYNC
			// then we stay in state NONE
			break;
		case HEADER:
			if (run == LONG) {

				if (length == 2 && last_run == SHORT && last_length == 8) {
					bit = 0;
				} else if (length == 4 && last_run == SHORT && last_length == 4) {
					bit = 1;
				} else {
					fprintf(stderr, "invalid bit found in state HEADER length = %d, last_length = %d\n", length, last_length);
					state = NONE;
					break;
				}
				bits++;
				if (bits == 0) {
					// start bit must be 0
					if (bit != 0) {
						fprintf(stderr, "invalid start bit found, must be 0 found 1\n");
						state = NONE;
						break;
					}
				} else if (bits == 9) {
					//VERBOSE("read header byte %02x", header[7-bytes]);
					//printf("%02x\n", header[7-bytes]);
					// stop bit must be 1
					if (bit != 1) {
						fprintf(stderr, "invalid stop bit found, must be 1 found 0\n");
						state = NONE;
						break;
					}
					bytes--;
					bits = -1;
				} else {
					header[7 - bytes] >>= 1;
					if (bit) header[7 - bytes] |= 0x80;
				}

				if (bytes == 0) {
					unsigned short last_addr;
					filename = le16toh(*(unsigned short*)&header[0]);
					first_addr = le16toh(*(unsigned short*)&header[2]);
					last_addr = le16toh(*(unsigned short*)&header[4]);
					checksum = header[6];
					data_size = bytes = last_addr - first_addr + 1;
					if (bytes > MAX_DATA_SIZE) {
						fprintf(stderr, "data size %d > %d bytes, not supported; increase MAX_DATA_SIZE", bytes, MAX_DATA_SIZE);
						state = NONE;
						break;
					}
					VERBOSE("header: filename=%04x, first_addr=%04x, last_addr=%04x, checksum=%02x", filename, first_addr, last_addr, checksum);
					state = MID_SYNC;
					assert(bits == -1);
				}
			}
			break;
		case MID_SYNC:
			// mid sync must be at least 1.5 seconds, so that the
			// MFP-1 can display the filename for 1.5 seconds before
			// reading the data
			if (run == SHORT && length > 3000) { 
				state = FIRST_DATA;
				VERBOSE("found MID_SYNC, loading DATA (%d bytes)", data_size);
			} else {
				fprintf(stderr, "invalid run in MID_SYNC\n");
				state = NONE;
			}
			break;
		case FIRST_DATA:
			// the SHORT run of FIRST_DATA is contained in the
			// MID_SYNC
			// correct length based on length of current LONG run, 
			// update state and fall through
			if (run == LONG) {
				if (length == 4) {
					last_length = 4;
				} else if (length == 2) {
					last_length = 8;
				} else {
					fprintf(stderr, "invalid bit found in state FIRST_DATA\n");
					state = NONE;
					break;
				}
			} else {
				fprintf(stderr, "invalid run in FIRST_DATA\n");
				state = NONE;
				break;
			}
			state = DATA;
		case DATA:
			if (run == LONG) {
				if (length == 2 && last_run == SHORT && last_length == 8) {
					bit = 0;
				} else if (length == 4 && last_run == SHORT && last_length == 4) {
					bit = 1;
				} else {
					fprintf(stderr, "invalid bit found in state HEADER\n");
					state = NONE;
					break;
				}
				bits++;
				if (bits == 0) {
					// start bit must be 0
					if (bit != 0) {
						fprintf(stderr, "invalid start bit found, must be 0 found 1\n");
						state = NONE;
						break;
					}
				} else if (bits == 9) {
					checksum -= data[data_size-bytes];
					//printf("%02x\n", data[data_size-bytes]);
					// stop bit must be 1
					if (bit != 1) {
						fprintf(stderr, "invalid stop bit found, must be 1 found 0\n");
						state = NONE;
						break;
					}
					bytes--;
					bits = -1;
				} else {
					data[data_size - bytes] >>= 1;
					if (bit) data[data_size - bytes] |= 0x80;
				}
				if (bytes == 0) {
					if (checksum != 0x00) {
						fprintf(stderr, "invalid checksum\n");
						state = NONE;
						break;
					}
					VERBOSE("DATA OK");
					printf("%04x/%04x:", filename, first_addr);
					for (int i = 0; i < data_size; i++) printf("%02x", data[i]);
					printf("\n");
					fflush(stdout);
					state = TAIL_SYNC;
				}
			}
			break;
		case TAIL_SYNC:
			// the purpose of tail sync is not very clear, let's say
			// it needs to be at lease 0.5 seconds long to be valid
			if (run == SHORT && length > 1000) { 
				VERBOSE("found TAIL_SYNC");
				state = NONE;
			} else {
				fprintf(stderr, "invalid run in TAIL_SYNC\n");
				state = NONE;
			}
			break;
	}

	last_run = run; last_length = length;

}

void decode(double duration) {
	static int phase = 1;
	static int length = 0;
	static double last_duration = 0, last_period = 0, last_goodness = 0;
	double period, goodness, best_period;
	static run_t run = SHORT, cur_run;

	period = duration + last_duration;

	/* the MPF uses signals of 1kHz (period 1ms)
	 * and signals of 2kHz (period 0.5ms)
	 *
	 * if the phase is wrongly interpreted, periods
	 * of .5ms, .75ms and 1ms are detected;
	 * if the phase is correctly interpreted, only
	 * periods of .5 and 1ms are detected
	 *
	 * in the below illustration, the phase is negative ( \ first )
	 *
	 *  wrong phase interpretation
	 *
	 *      .75ms       .75m       .5ms     .75ms          1ms
	 *  +-----------+-----------+-------+-----------+----------------+
	 *  | _         | _____     | _     | _         | ______         |
	 *  |/ \        |/     \    |/ \    |/ \        |/      \        |
	 *  /   \       /       \   /   \   /   \       /        \       /
	 *      |\_____/        |\_/    |\_/    |\_____/         |\_____/
	 *      |               |       |       |                |
	 *  ----+---------------+-------+-------+----------------+--------
	 *             1ms        .5ms    .5ms         1ms
	 *
	 * correct phase interpretation
	 *
	 * the goodness of a period is a measure of
	 * the distance from 75ms
	 *
	 * in this way this program attempts to guess
	 * the correct polarity every time....  */
	goodness = fabs(log(period/0.00075));

	if (goodness > last_goodness)  best_period = period;
	else best_period = last_period;

	last_goodness = goodness;
	last_period = period;
	last_duration = duration;

	/* only every second call to this function
	 * should result in the detection of a wave */
	if (phase++) {
		phase = 0;
		return;
	}

	if (best_period < M_SQRT1_2/1000) cur_run = SHORT;
	else cur_run = LONG;

	if (cur_run != run) {
		decode_inner(run, length);
		run = cur_run;
		length = 0;
	}

	length++;
}

int main(int argc, char *argv[]) {
	double duration;
	int last = -1;
	int val;

	verbose_init(argv[0]);

	while ((val = fgetc(stdin)) != EOF) {
		if (last != -1) {
			if ((last < 0x80 && val < 0x80) || (last >= 0x80 && val >= 0x80)) {
				duration += DURATION;
			} else {
				/* at this point we know that last != val
				 * zero crossing is at 127.5 = (255 + 0)/2.
				 * do a linear approximation to find time of
				 * zero crossing
				 *
				 * t is time of the crossing
				 *
				 * (val - last)/DURATION*t + last = 127.5
				 *
				 * =>
				 *
				 * t = (127.5-last)*DURATION/(val-last) */
				double t = (127.5-last)*DURATION/(val - last);
				duration += t;
				decode(duration);
				duration = DURATION - t;
			}
		}
		last = val;
	}

	decode(0); // to detect the last run

	exit(0);
}

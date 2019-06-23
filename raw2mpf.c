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

typedef enum state_e { NONE, HEADER, HEADER_SEPARATOR, MID_SYNC,
	FIRST_DATA, DATA, DATA_SEPARATOR, TAIL_SYNC } state_t;

const char *state_string(state_t state) {
	switch (state) {
		case NONE:
			return "NONE";
			break;
		case HEADER:
			return "HEADER";
			break;
		case HEADER_SEPARATOR:
			return "HEADER_SEPARATOR";
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
		case DATA_SEPARATOR:
			return "DATA_SEPARATOR";
			break;
		case TAIL_SYNC:
			return "TAIL_SYNC";
			break;
		default:
			assert(0);
	}
}

typedef enum run_e { SHORT, LONG } run_t;

// this macro is used in decode_wave() and in decode_run()
#define ODD (count%2)

#define PREV_LENGTH (lengths[!ODD])

// the first time this function is called, it is notified
// of a SHORT run, the second time of a LONG run etc
// the variable count keeps track of this
void decode_run(int length, int polarity) {
	static int count = 0;
	static state_t state = NONE;
	static int bits, bit, data_size;
	static int lengths[2] = { 1, 0 };
	static unsigned char header[7], data[MAX_DATA_SIZE], checksum;
	static unsigned short first_addr, filename;
	static unsigned char *ptr, *end_ptr;

	count++;

	/* of a LONG run is signalled ODD == false and if
	 * a SHORT run is signalled ODD = true
	 *
	 * the length of the last LONG run is stored in lengths[0]
	 * and the length of the last SHORT run is stored in lengths[1] */
	lengths[ODD] = length;

	/* according to MPF-1 monitor source listing: LEAD_SYNC is
	 * accepted if it is longer than 1 second a LEAD_SYNC always
	 * resets the state machine */
	if (!ODD && lengths[ODD] > 1000) {
		if (state != NONE) ERROR("LEAD_SYNC found while in state %s", state_string(state));
		ptr = header;
		end_ptr = header + 7; // expecting 7 bytes
		bits = -1; // expecting bit 0 (startbit)
		state = HEADER_SEPARATOR;
		memset(header, 0, sizeof(header));
		memset(data, 0, sizeof(data));
		VERBOSE("found %.1fs LEAD_SYNC, loading HEADER, %s polarity", lengths[ODD]/1000., polarity?"positive":"negative");
		return;

	}

	switch (state) {
		case NONE:
			/* if we are in state NONE and there is no LEAD_SYNC
			 * then we do nothing */
			break;
		case FIRST_DATA: /* called with LONG run */
			/* the SHORT run of FIRST_DATA is contained in the
			 * MID_SYNC
			 * correct length based on length of current LONG run, 
			 * update state and fall through
			 *
			 * if length == 2, then last_length will be set to 8
			 * if length == 4, then last length will be set to 4
			 * otherwise both will be invalid */
			PREV_LENGTH = -2*length + 12;
			state = DATA;
			/* fall through to DATA */
		case DATA:   /* called with LONG run */
		case HEADER: /* called with LONG run */
			if (length == 2 && PREV_LENGTH == 8) bit = 0;
			else if (length == 4 && PREV_LENGTH == 4) bit = 1;
			else {
				ERROR("invalid bit found in state %s length = %d, last_length = %d", state_string(state), length, PREV_LENGTH);
				state = NONE;
				break;
			}
			bits++;
			if (bits == 0) {
				// start bit should be 0
				if (bit != 0) WARNING("invalid start bit found, must be 0 found 1");
			} else if (bits == 9) {
				if (state == DATA) checksum -= *ptr;
				// stop bit should be 1
				if (bit != 1) WARNING("invalid stop bit found, must be 1 found 0");
				ptr++;
				bits = -1;
			} else {
				*ptr >>= 1;
				if (bit) *ptr |= 0x80;
			}

			if (state == HEADER) {
				if (ptr == end_ptr) {
					unsigned short last_addr;
					filename = le16toh(*(unsigned short*)&header[0]);
					first_addr = le16toh(*(unsigned short*)&header[2]);
					last_addr = le16toh(*(unsigned short*)&header[4]);
					checksum = header[6];
					data_size = last_addr - first_addr + 1;
					if (data_size > MAX_DATA_SIZE) {
						ERROR("data size %d > %d bytes, not supported; increase MAX_DATA_SIZE", data_size, MAX_DATA_SIZE);
						state = NONE;
						break;
					}
					ptr = data;
					end_ptr = data + data_size;
					VERBOSE("header: filename=%04x, first_addr=%04x, last_addr=%04x, checksum=%02x", filename, first_addr, last_addr, checksum);
					state = MID_SYNC;
					assert(bits == -1);
				} else state = HEADER_SEPARATOR;
			} else if (state == DATA) {
				if (ptr == end_ptr) {
					if (checksum != 0x00) {
						ERROR("invalid checksum");
						state = NONE;
						break;
					}
					VERBOSE("DATA OK");
					printf("%04x/%04x:", filename, first_addr);
					for (int i = 0; i < data_size; i++) printf("%02x", data[i]);
					printf("\n");
					fflush(stdout);
					state = TAIL_SYNC;
				} else state = DATA_SEPARATOR;
			}
			break;
		case HEADER_SEPARATOR: /* called with SHORT run */
			state = HEADER;
			break;
		case MID_SYNC: /* called with SHORT run */
			// mid sync must be at least 1.5 seconds, so that the
			// MFP-1 can display the filename for 1.5 seconds before
			// reading the data
			//
			// strictly speaking the length of MID_SYNC can't be
			// known at this point, because the last 4 or 8 waves
			// belong to the bit following the sync
			if (length>= 2900) {  /* use this, so that if the duration is
						* shorter, the time in seconds
						* rounded to one decimal is
						* actually smaller than 1.5s,
						* otherwise the error message
						* may not make sense */
				state = FIRST_DATA;
				VERBOSE("found %.1fs MID_SYNC, loading DATA (%d bytes)", length/2000., data_size);
			} else {
				ERROR("duration of MID_SYNC is too short %.1fs < %.1fs", length/2000., 1.5);
				state = NONE;
			}
			break;
		case DATA_SEPARATOR: /* called with SHORT run */
			state = DATA;
			break;
		case TAIL_SYNC: /* called with SHORT run */
			// the purpose of tail sync is not very clear, let's say
			// it needs to be at lease 0.5 seconds long to be valid
			if (length >= 900) VERBOSE("found %.fs TAIL_SYNC", length/2000.);
			else ERROR("duration of TAIL_SYNC is too short %.1fs < %.1fs", length/2000., 1.5);
			state = NONE;
			break;
	}
}

// the first time this function is called, it is notified
// of a rising edge, the second time of a falling edge etc
// the variable count keeps track of this
void decode_wave(double duration) {
	// count counts the number of times this function was called
	// if (postincrement) count is odd, then we were called at a rising edge
	// and if (postincrement) count is even, then we were called at a
	// falling edge
	static int count = 0;
	int polarity;
	static int length = 0;
	static double periods[2] = { 0, 0 }, durations[2] = { 0, 0 }, goodnesses[2] = { 0, 0 };
	static run_t run = SHORT; // this ensures that decode_run is called first with a SHORT run
	run_t cur_run;

	count++;

	durations[ODD] = duration;
	periods[ODD] = durations[0] + durations[1];

	/* the MPF uses signals of 1kHz (period 1ms)
	 * and signals of 2kHz (period 0.5ms)
	 *
	 * if the phase is wrongly interpreted, periods
	 * of .5ms, .75ms and 1ms are detected;
	 * if the phase is correctly interpreted, only
	 * periods of .5 and 1ms are detected
	 *
	 * in the below illustration, the polarity is negative ( \ first )
	 *
	 *  wrong polarity
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
	 * correct polarity
	 *
	 * the goodness of a period is a measure of
	 * the distance from 75ms
	 *
	 * in this way this program attempts to guess
	 * the correct polarity every time.... 
	 *
	 * Why not just use one side (up or down) and multiply by 2 to get the
	 * duration? This only works if the signal is sufficiently symmetrictal
	 * and it turned out that, in practice, that is not true */
	goodnesses[ODD] = fabs(log(periods[ODD]/0.00075));

	/* only every second call to this function
	 * should result in the detection of a wave */

	if (ODD) return;

	if (goodnesses[0] > goodnesses[1]) polarity = 0;
	else polarity = 1;

	cur_run = (periods[polarity] < 0.00075)?SHORT:LONG;

	length++; // increment length of current run

	if (cur_run == run) return;

	decode_run(length, polarity);

	run = cur_run;
	length = 0;
}

int main(int argc, char *argv[]) {
	double duration = 0;
	int last = 0; // this ensures that decode_wave() is first called on a rising edge
	int val;

	verbose_init(argv[0]);

	while ((val = fgetc(stdin)) != EOF) {
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
			decode_wave(duration);
			duration = DURATION - t;
		}
		last = val;
	}

	// end with 1 LONG wave to force detection of the TAIL_SYNC
	decode_wave(8*DURATION);
	decode_wave(8*DURATION);

	exit(0);
}

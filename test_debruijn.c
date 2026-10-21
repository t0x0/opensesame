/*
 * host (gcc) check for the de bruijn / bitstream generator in rf.c
 *
 * Builds rf.c in LOCAL mode (see types.h), captures every packet handed to
 * rftx() and asserts that every garage in garages.h puts every possible code
 * on the air.  Each packet carries the last `bits` symbols of the previous
 * one as context, so every code must appear inside a single packet.
 *
 * 	gcc -I. -O1 -fsanitize=address -o rf-test test_debruijn.c -lm && ./rf-test
 *	(or just: make test)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main rf_local_main
#include "rf.c"
#undef main

#define AIRMAX	(1 << 20)
#define SEP	0xff		/* breaks a run of decoded symbols */

static u8 air[AIRMAX];		/* decoded symbols, one per byte */
static u32 airlen;
static u32 packets;

static void put(u8 sym)
{
	if (airlen >= AIRMAX) {
		fprintf(stderr, "capture overflow\n");
		exit(1);
	}
	air[airlen++] = sym;
}

void tx_capture(void)
{
	u32 z, chunk;
	u8 t, sym, base = tri ? 3 : 2;

	if (airlen)
		put(SEP);		/* never test a window across two packets */
	packets++;

	/* the whole buffer goes on air (PKTLEN == MAXLEN); decode each len-bit
	 * chunk back into the symbol it encodes */
	for (z = 0; z + len <= MAXLEN * 8; z += len) {
		chunk = 0;
		for (t = 0; t < len; t++)
			chunk = (chunk << 1) | ((realbuf[(z + t) / 8] >> (7 - ((z + t) % 8))) & 1);

		sym = (u8) ((chunk == b0) ? 0 : (chunk == b1) ? 1 : (chunk == b2) ? 2 : SEP);

		put(sym < base ? sym : SEP);
	}
}

/* every code of n symbols must show up as a window inside one packet */
static int covers(u32 n, u32 base)
{
	u32 codes = 1, i, v;
	u8 *seen;
	u32 found = 0;
	u32 p;
	int missing = 0;

	for (i = 0; i < n; i++)
		codes *= base;

	seen = calloc((size_t) codes, 1);

	for (i = 0; i + n <= airlen; i++) {
		v = 0;
		for (p = 0; p < n; p++) {
			if (air[i + p] == SEP)
				break;
			v = v * base + air[i + p];
		}
		if (p == n && !seen[v]) {
			seen[v] = 1;
			found++;
		}
	}

	for (i = 0; i < codes; i++)
		if (!seen[i])
			missing++;

	free(seen);

	printf("%s  %lu/%lu codes, %lu packet(s), %lu bits on air\n",
		missing ? "FAIL" : "ok  ",
		(unsigned long) found, (unsigned long) codes,
		(unsigned long) packets, (unsigned long) (packets * MAXLEN * 8));

	return missing == 0;
}

int main(void)
{
	int id, ok = 1;

	for (id = 0; id < (int) (sizeof(garages) / sizeof(garages[0])); id++) {
		_garage_id = (u8) id;
		airlen = 0;
		packets = 0;
		memset(realbuf, 0, sizeof(realbuf));

		printf("garage %d: %lu Hz, %lu baud, %u bits, %u symbols: ", id,
			(unsigned long) garages[id].hz, (unsigned long) garages[id].baud,
			garages[id].bits, garages[id].tri ? 3 : 2);

		db_send();

		if (!covers(garages[id].bits, garages[id].tri ? 3 : 2))
			ok = 0;
	}

	/* widest sequence the chip is configured for: this is where realbuf
	 * runs off the end if the transmit threshold is cut too fine */
	printf("widest (%u symbols): ", MAXBITS);
	bits = MAXBITS;
	len = 4;
	tri = 0;
	b0 = 0x8;
	b1 = 0xe;
	b2 = 0;
	airlen = 0;
	packets = 0;
	memset(realbuf, 0, sizeof(realbuf));
	de_bruijn();
	if (!covers(MAXBITS, 2))
		ok = 0;

	printf("%s\n", ok ? "all key spaces covered" : "FAILED");
	return !ok;
}

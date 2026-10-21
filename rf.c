// functions for garage pwnage and de bruijn sequence generation
// for the im-me and opensesame garage hacking platform
//
// -samy kamkar
// http://samy.pl/opensesame

#include "types.h"
#ifndef LOCAL
#include <cc1110.h>
#endif
#include "display.h"
#include "garages.h"
#include "rf.h"
#include <math.h>
#include <stdio.h>

// we're making these global because
// we have limited stack and we're
// calling some functions recursively
__xdata u8 a[MAXBITS+1]; // largest bit size possible
u16 s, k;
u16 txbits;               // bits of realbuf filled by the previous transmission
u8 tmpi;
__bit firstTx;

// we could read from garages[] directly but this is faster
u8 bits, len;
__bit tri;
u32 b0, b1, b2;

// db() recurses, and sdcc keeps locals static without --stack-auto, so the
// loop counter is kept per recursion depth instead of in a local
u8 jj[MAXBITS+2];
//#define bits g.bits
//#define len  g.len
//#define tri  g.tri
//#define b0   g.b0
//#define b1   g.b1

#ifdef LOCAL
// local (gcc) build: hand each finished packet to the test driver instead
// of driving the radio
extern void tx_capture(void);
void rftx() { tx_capture(); }
void waitForTx() { }
void printl(u8 line, char *str) { (void) line; (void) str; }
u8 _garage_id; // normally defined (and set) by opensesame.c
u8 sequence[MAXLEN];
#else
__xdata u8 sequence[MAXLEN];
#endif

// set or clear bit b in array A
void setBit(u8 *A, u16 b, __bit val)
{
	val ?
		(A[b / 8] |=   1 << (b % 8)) :
		(A[b / 8] &= ~(1 << (b % 8)));
}

// test bit b in array A
__bit testBit(u8 *A, u16 b)
{
	return ( ( A[b / 8] & (1 << (b % 8) ) ) != 0 );
}

// trinary garages have a third symbol, so a symbol is stored in 2 bits
void setSym(u16 idx, u8 val)
{
	setBit(sequence, idx * 2,     val & 1);
	setBit(sequence, idx * 2 + 1, val >> 1);
}

u8 testSym(u16 idx)
{
	return (u8) (testBit(sequence, idx * 2) ? 1 : 0)
	     + (u8) (testBit(sequence, idx * 2 + 1) ? 2 : 0);
}

// append one symbol to the de bruijn sequence, transmitting whenever realbuf
// is full.  bits-1 more symbols are always still needed for the tail, so
// leave room for them or convert_bits() overruns realbuf.
void emit(u8 val)
{
	setSym(s++, val);

	if ((s + bits) * len >= MAXLEN * 8)
		doTx();
}

// binary de bruijn sequence -samyk
void db(u8 t, u8 p)
{
	u8 j;

	if (t > bits)
	{
		if (bits % p == 0)
		{
			for (j = 1; j <= p; j++)
				emit(a[j]);
		}
	}
	else
	{
		a[t] = a[t-p];

		db(t+1, p);
		for (jj[t] = a[t-p] + 1; jj[t] <= (tri ? 2 : 1); jj[t]++)
		{
			a[t] = jj[t];
			db(t+1, t);
		}
	}
}

// realbuf is transmitted byte by byte, MSB first, so logical bit k lives in
// the top bit of byte k/8.  Everything that touches realbuf has to go through
// this mapping, both to hit the air in the right order and to stay
// translatable when a packet is shifted inside the buffer.
#define RB(k) (u16) ((k) / 8 * 8 + (7 - (k) % 8))

// converts our bits to the actual bitstream/modulation the garage expects
void convert_bits()
{
	u8 tlen;
	u16 z;
	u32 tb;
	u8 sym;

	// pull in the last code's worth of bits of the last transmission, so the
	// garage's shift register keeps its context over the gap between packets.
	// s and k are already the counters of *this* pass, so what realbuf holds
	// comes from the previous one (txbits).
	if (!firstTx)
	{
		for (z = 0; z < (u16) bits * len; z++)
			setBit(realbuf, RB(z), testBit(realbuf, RB(txbits - (u16) bits * len + z)));
	}
	else
		firstTx = 0;

	// go through each bit and convert to the garage bitstream equivalent
	// eg 0 = 1000, 1 = 1110
	for (z = k / len; z < s; z++)
	{
		sym = testSym(z);
		tb = (sym == 2) ? b2 : (sym ? b1 : b0);

		tlen = len;
		while (tlen--)
		{
			// there's gotta be a better way to do this but my brain hurts right now
			setBit(realbuf, RB(k), (tb >> tlen) & 1);
			k++;
		}
	}

	// this pass filled realbuf up to here; the next one reads its overlap from it
	txbits = s * len;

	// start 10 bits in next time (assuming 10 bit code)
	k = bits * len;
	s = bits;
}

// start the de bruijn sequence using the recursive db() function
void de_bruijn()
{
	// clear a[], s, k
	for (tmpi = 0; tmpi <= MAXBITS; tmpi++)
		a[tmpi] = 0;
	s = 0;
	k = 0;
	txbits = 0;
	firstTx = 1;

	// begin recursive de bruijn
	db(1, 1);

	// a real de bruijn wraps, but since we can't wrap time we have to send the last of the bits to complete the sequence
	for (tmpi = 0; tmpi < bits-1; tmpi++)
		emit(0);

	// transmit anything left over
	doTx();
}

// set FREQ registers in cc111x from a float
void setFreq(char *FREQ2, char *FREQ1, char *FREQ0)
{
#define fnum (u32)(g.hz * ((0x10000 / 1000000.0) / MHZ))
	*FREQ2 = fnum >> 16;
	*FREQ1 = (fnum >> 8) & 0xFF;
	*FREQ0 = fnum & 0xFF;
}

// set baudrate registers
void setBaud(float drate, char *MDMCFG4, char *MDMCFG3)
{
	u8 drate_e = 0;
	u8 drate_m = 0;
	float m = 0;

	for (tmpi = 0; tmpi < 16; tmpi++)
	{

		m = (g.baud * powf(2,28) / (powf(2, tmpi)* (MHZ*1000000.0))-256) + .5;
		if (m < 256)
		{
			drate_e = tmpi;
			drate_m = m;
			break;
		}
	}

	//drate = 1000000.0 * MHZ * (256+drate_m) * powf(2,drate_e) / powf(2,28);

	*MDMCFG3 = drate_m;
#ifndef MDMCFG4_DRATE_E
#define MDMCFG4_DRATE_E 0x0F
#endif
	*MDMCFG4 &= ~MDMCFG4_DRATE_E;
	*MDMCFG4 |= drate_e;
}

// prepare our transmission and keep the end for
// the next transmission as the delay between packets
// may prevent the de bruijn exploit from working
void doTx()
{
	// don't modify realbuf until we're done transmitting
	// previous data since we're using DMA to TX
	waitForTx();

	// modify our realbuf to the real sequence of bits we need to send
	convert_bits();

	// begin transmitting
	rftx();
}

// create a de bruijn sequence according to what we want here
void db_send()
{
	// set power
	// maximum power
	PA_TABLE0 = 0x00;
	if (g.hz <= 400000000)
		PA_TABLE1 = 0xC2;
	else if (g.hz <= 464000000)
		PA_TABLE1 = 0xC0;
	else if (g.hz <= 900000000)
		PA_TABLE1 = 0xC2;
	else
		PA_TABLE1 = 0xC0;

	// set frequency
	setFreq(g.hz, &FREQ2, &FREQ1, &FREQ0);
	CHANNR = 0x00;

	// maximum channel bandwidth
	setBaud(g.baud, &MDMCFG4, &MDMCFG3);

	bits    = g.bits;
	len     = g.len;
	tri     = g.tri;
	b0      = g.b0;
	b1      = g.b1;
	b2      = g.b2;

	// create and send our de bruijn bitstream
	de_bruijn();
}

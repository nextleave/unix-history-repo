/* 
 *  Routines in this file based on the work of Volker Lendecke,
 *  Adapted for ncplib by Boris Popov
 *  Please note that ncpl_crypt.c file should be indentical to this one
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <string.h>

/*$*********************************************************
   $*
   $* This code has been taken from DDJ 11/93, from an 
   $* article by Pawel Szczerbina.
   $*
   $* Password encryption routines follow.
   $* Converted to C from Barry Nance's Pascal
   $* prog published in the March -93 issue of Byte.
   $*
   $* Adapted to be useable for ncpfs by 
   $* Volker Lendecke <lendecke@namu01.gwdg.de> in 
   $* October 1995. 
   $*
   $********************************************************* */



typedef unsigned char buf32[32];

static unsigned char encrypttable[256] = {
0x7, 0x8, 0x0, 0x8, 0x6, 0x4, 0xE, 0x4, 0x5, 0xC, 0x1, 0x7, 0xB, 0xF, 0xA, 0x8,
0xF, 0x8, 0xC, 0xC, 0x9, 0x4, 0x1, 0xE, 0x4, 0x6, 0x2, 0x4, 0x0, 0xA, 0xB, 0x9,
0x2, 0xF, 0xB, 0x1, 0xD, 0x2, 0x1, 0x9, 0x5, 0xE, 0x7, 0x0, 0x0, 0x2, 0x6, 0x6,
0x0, 0x7, 0x3, 0x8, 0x2, 0x9, 0x3, 0xF, 0x7, 0xF, 0xC, 0xF, 0x6, 0x4, 0xA, 0x0,
0x2, 0x3, 0xA, 0xB, 0xD, 0x8, 0x3, 0xA, 0x1, 0x7, 0xC, 0xF, 0x1, 0x8, 0x9, 0xD,
0x9, 0x1, 0x9, 0x4, 0xE, 0x4, 0xC, 0x5, 0x5, 0xC, 0x8, 0xB, 0x2, 0x3, 0x9, 0xE,
0x7, 0x7, 0x6, 0x9, 0xE, 0xF, 0xC, 0x8, 0xD, 0x1, 0xA, 0x6, 0xE, 0xD, 0x0, 0x7,
0x7, 0xA, 0x0, 0x1, 0xF, 0x5, 0x4, 0xB, 0x7, 0xB, 0xE, 0xC, 0x9, 0x5, 0xD, 0x1,
0xB, 0xD, 0x1, 0x3, 0x5, 0xD, 0xE, 0x6, 0x3, 0x0, 0xB, 0xB, 0xF, 0x3, 0x6, 0x4,
0x9, 0xD, 0xA, 0x3, 0x1, 0x4, 0x9, 0x4, 0x8, 0x3, 0xB, 0xE, 0x5, 0x0, 0x5, 0x2,
0xC, 0xB, 0xD, 0x5, 0xD, 0x5, 0xD, 0x2, 0xD, 0x9, 0xA, 0xC, 0xA, 0x0, 0xB, 0x3,
0x5, 0x3, 0x6, 0x9, 0x5, 0x1, 0xE, 0xE, 0x0, 0xE, 0x8, 0x2, 0xD, 0x2, 0x2, 0x0,
0x4, 0xF, 0x8, 0x5, 0x9, 0x6, 0x8, 0x6, 0xB, 0xA, 0xB, 0xF, 0x0, 0x7, 0x2, 0x8,
0xC, 0x7, 0x3, 0xA, 0x1, 0x4, 0x2, 0x5, 0xF, 0x7, 0xA, 0xC, 0xE, 0x5, 0x9, 0x3,
0xE, 0x7, 0x1, 0x2, 0xE, 0x1, 0xF, 0x4, 0xA, 0x6, 0xC, 0x6, 0xF, 0x4, 0x3, 0x0,
0xC, 0x0, 0x3, 0x6, 0xF, 0x8, 0x7, 0xB, 0x2, 0xD, 0xC, 0x6, 0xA, 0xA, 0x8, 0xD
};

static buf32 encryptkeys = {
    0x48, 0x93, 0x46, 0x67, 0x98, 0x3D, 0xE6, 0x8D,
    0xB7, 0x10, 0x7A, 0x26, 0x5A, 0xB9, 0xB1, 0x35,
    0x6B, 0x0F, 0xD5, 0x70, 0xAE, 0xFB, 0xAD, 0x11,
    0xF4, 0x47, 0xDC, 0xA7, 0xEC, 0xCF, 0x50, 0xC0
};

/*
 * Create table-based 16-bytes hash from a 32-bytes array
 */
static void
nw_hash(buf32 temp, unsigned char *target) {
	short sum;
	unsigned char b3;
	int s, b2, i;

	sum = 0;

	for (b2 = 0; b2 <= 1; ++b2) {
		for (s = 0; s <= 31; ++s) {
			b3 = (temp[s] + sum) ^ (temp[(s + sum) & 31] - encryptkeys[s]);
			sum += b3;
			temp[s] = b3;
		}
	}

	for (i = 0; i <= 15; ++i) {
		target[i] = encrypttable[temp[2 * i]]
		    | (encrypttable[temp[2 * i + 1]] << 4);
	}
}


/*
 * Create a 16-bytes pattern from given buffer based on a four bytes key
 */
void
nw_keyhash(const u_char *key, const u_char *buf, int buflen, u_char *target) {
	int b2, d, s;
	buf32 temp;

	while (buflen > 0 && buf[buflen - 1] == 0)
		buflen--;

	bzero(temp, sizeof(temp));

	d = 0;
	while (buflen >= 32) {
		for (s = 0; s <= 31; ++s)
			temp[s] ^= buf[d++];
		buflen -= 32;
	}
	b2 = d;
	if (buflen > 0)	{
		for (s = 0; s <= 31; ++s) {
			if (d + buflen == b2) {
				temp[s] ^= encryptkeys[s];
				b2 = d;
			} else
				temp[s] ^= buf[b2++];
		}
	}
	for (s = 0; s <= 31; ++s)
		temp[s] ^= key[s & 3];

	nw_hash(temp, target);
}

/*
 * Create an 8-bytes pattern from an 8-bytes key and 16-bytes of data
 */
void
nw_encrypt(const u_char *fra, const u_char *buf, u_char *target) {
	buf32 k;
	int s;

	nw_keyhash(fra, buf, 16, k);
	nw_keyhash(fra + 4, buf, 16, k + 16);

	for (s = 0; s < 16; s++)
		k[s] ^= k[31 - s];

	for (s = 0; s < 8; s++)
		*target++ = k[s] ^ k[15 - s];
}



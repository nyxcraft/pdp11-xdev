/*
 * softfp.c -- PDP-11 D-format (56-bit) soft floating point for c1's
 * float-constant conversion.
 *
 * The host build of c1 previously converted float-constant text with the
 * host's atof (IEEE double, 53-bit mantissa) and zero-extended to the
 * 56-bit D format on output.  A real PDP-11 -- FP11 hardware and the 2.8
 * fpsim interpreter alike -- carries the full 56-bit mantissa and rounds
 * every operation by adding one to the most significant discarded bit
 * (round-half-up on the first guard bit; there is no sticky bit and no
 * round-to-even).  For decimal fractions whose conversion is inexact the
 * two disagree in the last bits of the mantissa (e.g. ecvt.c's .03:
 * ...c0 host vs ...c3 PDP-11).
 *
 * This file computes the constant the way the 1981 machine did: the exact
 * operation sequence of 2.8 libc atof.c, executed in soft D-format
 * arithmetic, yielding the four .data words directly.
 *
 * D format: word0 = sign(1) | excess-128 exponent(8) | fraction(7),
 * words 1..3 = 48 more fraction bits; hidden leading bit; value =
 * 0.1fffff... * 2^(exp-128).
 */

#include <ctype.h>

#define LOGHUGE 39	/* 2.8 math.h */

typedef unsigned long long u64;
typedef unsigned __int128 u128;

/* internal form: value = frac * 2^(exp-56), frac normalized to bit 55
 * (so 1.0 is frac=1<<55, exp=1); frac==0 means zero. */
struct sd {
	int sign;	/* 0 or 1 */
	int exp;	/* binary exponent, D-format excess-128 semantics:
			 * value = 0.frac * 2^exp with frac's MSB at bit 55 */
	u64 frac;	/* 56 significant bits, MSB at bit 55 when nonzero */
};

static struct sd
sd_zero(void)
{
	struct sd z; z.sign = 0; z.exp = 0; z.frac = 0; return z;
}

/* normalize a 128-bit intermediate whose value is v * 2^(exp-120)
 * (i.e. treat bit 119 as the D fraction MSB position), then round to
 * 56 bits FP11-style: add one at the first discarded bit. */
static struct sd
sd_norm128(int sign, int exp, u128 v)
{
	struct sd r;
	int sh;

	if (v == 0)
		return sd_zero();
	/* bring MSB to bit 119 */
	while (v >> 120) { v >>= 1; exp++; }
	while ((v >> 119) == 0) { v <<= 1; exp--; }
	/* keep bits 119..64 (56 bits); round on bit 63 */
	sh = 64;
	{
		u64 frac = (u64)(v >> sh);
		u64 guard = (u64)(v >> (sh-1)) & 1;
		if (guard) {
			frac++;
			if (frac >> 56) {	/* rounding overflowed */
				frac >>= 1;
				exp++;
			}
		}
		r.sign = sign; r.exp = exp; r.frac = frac;
	}
	return r;
}

static struct sd
sd_fromint(long v)
{
	struct sd r;
	int sign = 0;
	u128 m;

	if (v == 0)
		return sd_zero();
	if (v < 0) { sign = 1; v = -v; }
	m = (u128)(u64)v << 64;		/* value = m * 2^(exp-120) with exp=56 */
	r = sd_norm128(sign, 56, m);
	return r;
}

static struct sd
sd_mul(struct sd a, struct sd b)
{
	u128 p;

	if (a.frac == 0 || b.frac == 0)
		return sd_zero();
	/* a.frac,b.frac have MSB at bit 55: product MSB at bit 110/111.
	 * value = p * 2^(a.exp+b.exp-112); align to the *2^(exp-120) frame:
	 * shift left 8 so p' = p<<8, exp' = a.exp+b.exp. */
	p = (u128)a.frac * b.frac;
	return sd_norm128(a.sign ^ b.sign, a.exp + b.exp, p << 8);
}

static struct sd
sd_div(struct sd a, struct sd b)
{
	u128 q;

	if (a.frac == 0)
		return sd_zero();
	/* quotient of fractions in [0.5,1): q in (0.5,2).
	 * (a.frac<<64)/b.frac gives 64..65 bits; value = q * 2^(a.exp-b.exp-64).
	 * align into the 2^(exp-120) frame: q' = q<<56, exp' = a.exp-b.exp.
	 * The shift keeps every quotient bit; bits beyond the first guard are
	 * irrelevant under FP11 rounding (no sticky). */
	q = ((u128)a.frac << 64) / b.frac;
	return sd_norm128(a.sign ^ b.sign, a.exp - b.exp, q << 56);
}

static struct sd
sd_add(struct sd a, struct sd b)
{
	u128 ma, mb;
	int exp, d;

	if (a.frac == 0) return b;
	if (b.frac == 0) return a;
	if (a.exp < b.exp || (a.exp == b.exp && a.frac < b.frac)) {
		struct sd t = a; a = b; b = t;
	}
	/* a now has the larger magnitude; align b to a's exponent inside a
	 * 120-bit frame (64 guard bits -- beyond FP11's alignment but only
	 * the first guard bit decides the rounding, and lower bits can only
	 * matter through borrow on subtraction, which the FP11 also keeps). */
	exp = a.exp;
	ma = (u128)a.frac << 64;
	d = a.exp - b.exp;
	if (d > 63)
		mb = 0;
	else
		mb = ((u128)b.frac << 64) >> d;
	if (a.sign == b.sign) {
		ma += mb;
	} else {
		ma -= mb;
		if (ma == 0)
			return sd_zero();
	}
	return sd_norm128(a.sign, exp, ma);
}

static int
sd_lt(struct sd a, struct sd b)	/* a < b, both nonnegative here */
{
	if (a.frac == 0) return b.frac != 0;
	if (b.frac == 0) return 0;
	if (a.exp != b.exp) return a.exp < b.exp;
	return a.frac < b.frac;
}

/* pack to the four D-format words (word0 first). */
static void
sd_towords(struct sd a, unsigned short w[4])
{
	int e;
	u64 f;

	if (a.frac == 0) {
		w[0] = w[1] = w[2] = w[3] = 0;
		return;
	}
	e = a.exp + 128;		/* excess-128 */
	if (e <= 0) {			/* underflow -> 0 */
		w[0] = w[1] = w[2] = w[3] = 0;
		return;
	}
	if (e > 255) e = 255;		/* overflow -> clamp */
	f = a.frac & ~(1ULL << 55);	/* drop hidden bit: 55 stored bits */
	w[0] = (a.sign << 15) | (e << 7) | (unsigned short)(f >> 48);
	w[1] = (unsigned short)(f >> 32);
	w[2] = (unsigned short)(f >> 16);
	w[3] = (unsigned short)f;
}

/*
 * softfp_atof: 2.8 libc/gen/atof.c transliterated onto the soft ops.
 * Same statements, same operation order, so every intermediate rounding
 * happens exactly where the 1981 compile's did.
 */
void
softfp_atof(char *p, unsigned short w[4])
{
	int c;
	struct sd fl, flexp, exp5, big, ten;
	int nd;
	int eexp, exp, neg, negexp, bexp;

	ten = sd_fromint(10L);
	big = sd_fromint(1L);
	big.exp += 56;			/* 2^56 */

	neg = 1;
	while ((c = *p++) == ' ')
		;
	if (c == '-')
		neg = -1;
	else if (c == '+')
		;
	else
		--p;

	exp = 0;
	fl = sd_zero();
	nd = 0;
	while ((c = *p++), isdigit(c)) {
		if (sd_lt(fl, big))
			fl = sd_add(sd_mul(ten, fl), sd_fromint((long)(c-'0')));
		else
			exp++;
		nd++;
	}

	if (c == '.') {
		while ((c = *p++), isdigit(c)) {
			if (sd_lt(fl, big)) {
				fl = sd_add(sd_mul(ten, fl), sd_fromint((long)(c-'0')));
				exp--;
			}
			nd++;
		}
	}

	negexp = 1;
	eexp = 0;
	if ((c == 'E') || (c == 'e')) {
		if ((c = *p++) == '+')
			;
		else if (c == '-')
			negexp = -1;
		else
			--p;
		while ((c = *p++), isdigit(c)) {
			eexp = 10*eexp + (c-'0');
		}
		if (negexp < 0)
			eexp = -eexp;
		exp = exp + eexp;
	}

	negexp = 1;
	if (exp < 0) {
		negexp = -1;
		exp = -exp;
	}

	if ((nd + exp*negexp) < -LOGHUGE) {
		fl = sd_zero();
		exp = 0;
	}
	flexp = sd_fromint(1L);
	exp5 = sd_fromint(5L);
	bexp = exp;
	for (;;) {
		if (exp & 01)
			flexp = sd_mul(flexp, exp5);
		exp >>= 1;
		if (exp == 0)
			break;
		exp5 = sd_mul(exp5, exp5);
	}
	if (negexp < 0)
		fl = sd_div(fl, flexp);
	else
		fl = sd_mul(fl, flexp);
	if (fl.frac != 0)
		fl.exp += negexp*bexp;	/* ldexp: exact exponent adjust */
	if (neg < 0)
		fl.sign ^= 1;
	sd_towords(fl, w);
}

/* exact long -> D words (for c1's LTOF constant fold) */
void
softfp_fromlong(long v, unsigned short w[4])
{
	sd_towords(sd_fromint(v), w);
}

/* D words -> F-format (single) words, FP11 rounding at 24 bits.
 * F format: word0 = s|exp|f(7) (same as D), word1 = 16 more fraction bits. */
void
softfp_dtof(unsigned short d[4], unsigned short f[2])
{
	unsigned short w[4];
	u64 frac;
	int e, sign;

	if ((d[0] & 077600) == 0) {	/* zero/underflow (exp 0) */
		f[0] = f[1] = 0;
		return;
	}
	sign = (d[0] >> 15) & 1;
	e = (d[0] >> 7) & 0377;
	frac = (1ULL << 55) | ((u64)(d[0] & 0177) << 48)
	     | ((u64)d[1] << 32) | ((u64)d[2] << 16) | d[3];
	/* keep 24 bits (MSB at bit 55): round on bit 31 */
	if ((frac >> 31) & 1) {
		frac += 1ULL << 32;
		if (frac >> 56) {	/* carry out of the hidden bit */
			frac >>= 1;
			e++;
			if (e > 255) e = 255;
		}
	}
	frac &= ~(1ULL << 55);
	w[0] = (sign << 15) | (e << 7) | (unsigned short)(frac >> 48);
	w[1] = (unsigned short)(frac >> 32);
	f[0] = w[0];
	f[1] = w[1];
}

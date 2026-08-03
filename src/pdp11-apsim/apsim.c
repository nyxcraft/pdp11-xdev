/*
 * apsim -- a tiny user-mode PDP-11 simulator for verifying this toolchain's
 * output end to end.  It loads a 2.8BSD a.out (0407/0410/0411), or a First
 * Edition one (0405: loaded whole at 040000, 1971-72 trap conventions, and
 * a KE11-A extended arithmetic element at 0177300), and executes
 * PDP-11 instructions: shared I&D (0407/0410) runs in one 64 KB space, while
 * separate I&D (0411 -- e.g. rogue) gets two 64 KB spaces, with instruction
 * fetches (opcodes, immediates, index/absolute words) reading I-space and all
 * data accesses reading D-space.  It emulates the handful of 2BSD `sys' traps
 * the libc stubs use (exit, read, write, open, close, creat, lseek, getuid,
 * ...).  Not a full or cycle-accurate PDP-11 -- just enough to run programs
 * built by cc/as/ld and observe their output and exit status.
 *
 * This is a host-side verification tool, NOT part of the produced toolchain.
 *
 * Usage:  apsim [-t] a.out [args...]      (-t traces each instruction)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <utime.h>
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include "universe.h"

/* ---- universes / kernel personalities --------------------------------
 * One canonical syscall switch (V7 numbering, the lineage trunk) plus
 * per-era remap tables and shape branches, following the VAX apsim's
 * design.  --universe/-u (or $PDP11_UNIVERSE) selects the personality;
 * a 0405 magic still auto-selects First Edition regardless (the same
 * role load-time refinement plays on the VAX side).
 *
 * Kern is era-ordered (enum pdp11_kern in universe.h), so lineage checks
 * read as ranges: `Kern >= PDP11_K_BSD210' = "the 4.3-numbered eras". */
static int Kern = PDP11_K_BSD2X;	/* personality (default: bsd29) */
static const char *Univ = PDP11_UNIV_DEFAULT_NAME;

static const struct { const char *name; int id, kern; } UnivTab[] = {
#define X(n, i, s, k, d) { n, i, k },
	PDP11_UNIVERSE_TABLE(X)
#undef X
	{ 0, 0, 0 }
};
static const struct { const char *alias, *canon; } UnivAlias[] = {
#define X(a, c) { a, c },
	PDP11_UNIVERSE_ALIASES(X)
#undef X
	{ 0, 0 }
};
static int stackargs;	/* syscall args on the user stack at 2(sp)... (the
			 * 2.10/2.11 kernels' C calling convention) instead of
			 * inline after the trap / fd in r0 (V1..2.9).  Set by
			 * the universe; -2 / APSIM_SYSARGS=stack override. */
static int univ_apply(const char *name){
	int i;
	for(i=0; UnivAlias[i].alias; i++)
		if(!strcmp(name, UnivAlias[i].alias)){ name=UnivAlias[i].canon; break; }
	for(i=0; UnivTab[i].name; i++)
		if(!strcmp(name, UnivTab[i].name)){
			Univ = UnivTab[i].name;
			Kern = UnivTab[i].kern;
			stackargs = (Kern >= PDP11_K_BSD210);
			return 1;
		}
	return 0;
}

/* Host errno -> guest errno.  The first 34 are the shared V7 inheritance
 * and pass through; past that the eras diverge: the 4.3-numbered eras
 * (bsd210/bsd211) moved EAGAIN to 35 (11 became EDEADLK) and added the
 * 35..90 range, while the classic eras never saw a number above 34 --
 * so anything unmappable collapses to a sane in-range meaning rather
 * than letting a Linux number through (Linux ELOOP=40 would read as
 * 2.9's ENOMSG-shaped nonsense). */
static int errno_h2g(int e){
	int modern = (Kern >= PDP11_K_BSD210);
	switch(e){
	case EAGAIN:       return modern ? 35 : 11;
	case EDEADLK:      return modern ? 11 : 5;
	case ELOOP:        return modern ? 62 : ENOENT;
	case ENAMETOOLONG: return modern ? 63 : ENOENT;
	case ENOTEMPTY:    return modern ? 66 : EEXIST;
	case EDQUOT:       return modern ? 69 : ENOSPC;
	case ESTALE:       return modern ? 70 : EIO;
	case ENOSYS:       return modern ? 78 : EINVAL;
	case ETIMEDOUT:    return modern ? 60 : EINTR;
	case ECONNREFUSED: return modern ? 61 : EIO;
	default:           return (e>=1 && e<=34) ? e : EIO;
	}
}

/* ---- terminal emulation (for curses programs like rogue) ----
 * The guest sees a 2BSD `sgttyb': sg_ispeed/ospeed/erase/kill (chars) + a
 * 16-bit sg_flags.  When the guest flips CBREAK/RAW/~ECHO via TIOCSETP, we
 * push the matching mode onto the host terminal so reads deliver one
 * keystroke at a time with no echo.  ttymode!=0 once we've gone raw, so the
 * host termios can be restored at exit. */
#define SG_CBREAK 02
#define SG_ECHO   010
#define SG_CRMOD  020
#define SG_RAW    040
static struct termios saved_tio; static int tty_saved=0, ttyraw=0;
static int guest_sgflags = SG_ECHO|SG_CRMOD;	/* cooked at start */
static int guest_lmode;				/* TIOCLGET/LSET local mode word */
static void tty_restore(void){ if(tty_saved && ttyraw){ tcsetattr(0,TCSANOW,&saved_tio); ttyraw=0; } }
static void tty_apply(int sgf){
	struct termios t;
	guest_sgflags=sgf;
	if(!tty_saved) return;
	t=saved_tio;
	if(sgf&(SG_RAW|SG_CBREAK)){		/* char-at-a-time input */
		t.c_lflag &= ~(ICANON);
		t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
		if(sgf&SG_RAW){ t.c_lflag &= ~(ISIG); cfmakeraw(&t); t.c_cc[VMIN]=1; t.c_cc[VTIME]=0; }
		ttyraw=1;
	} else { ttyraw=0; }
	if(!(sgf&SG_ECHO)) t.c_lflag &= ~ECHO; else t.c_lflag |= ECHO;
	tcsetattr(0,TCSANOW, ttyraw||!(sgf&SG_ECHO) ? &t : &saved_tio);
}

static unsigned char M[1<<16];		/* data space (also the shared I&D space) */
static unsigned char MI[1<<16];		/* separate instruction space (0411 only) */
static unsigned char *Isp = M;		/* where instructions are fetched: M (shared) or MI (sep I&D) */
static unsigned short R[8];		/* R6=sp, R7=pc */
#define SP R[6]
#define PC R[7]
static int FN, FZ, FV, FC;		/* condition codes */
static int halted, ecode, trace, systrace, watchtext, tsizew, watchsp, watchaddr=-1, watchval;
/* 0430 auto-overlay state: base-relative window at ov_base, ov_max bytes;
 * EMT (ovno in r0) copies overlay images in (2.9 csv.s ovhndlr protocol) */
static int ov_proc, ov_sep, ov_base, ov_max, ov_siz[16], cur_ovno;
static int gbrk;	/* program break (end of bss), tracked for break/sbrk */
static unsigned char ov_img[15][16384];

/* ---- process identity (uid model) ----
 * A real, mutable uid/gid so getuid/getgid are consistent, setuid/setgid take
 * effect, and getpwuid(getuid()) resolves the right /etc/passwd entry.  Default
 * 1/1 (matches the faux-root passwd entry play-rogue.sh installs); override
 * with $APSIM_UID / $APSIM_GID.  Sandbox policy: any setuid/setgid is allowed. */
static int g_ruid=1, g_euid=1, g_rgid=1, g_egid=1;
/* $APSIM_PID: force a fixed getpid() so programs that seed their RNG from the
 * pid (e.g. rogue: seed=getpid()) are deterministic -- lets the same game be
 * replayed and the original vs our-toolchain build be compared step for step.
 * 0 = use the real host pid. */
static int g_fakepid;
static int guest_pid(void){ return g_fakepid ? (g_fakepid&0x7fff) : (getpid()&0x7fff); }

/* ---- signal delivery ----
 * guest_sigh[s] is the guest's disposition for signal s (a 2.8 signal number):
 * 0=SIG_DFL, 1=SIG_IGN, else the handler address libc registered (a `tvect'
 * trampoline).  Async host signals are caught by hsig, which records the
 * *guest-numbered* signal in pending_sig; the main loop delivers it between
 * instructions, replicating 2.8's sendsig() (push PC and PS, set PC=handler,
 * reset the disposition -- V7 one-shot).
 *
 * 2.8 and Linux signal numbers agree for most but NOT all of 1-15 (2.8 EMT=7
 * is Linux SIGBUS, 2.8 BUS=10 is Linux SIGUSR1, 2.8 SYS=12 is Linux SIGSYS=31),
 * so kill() and the catch path translate explicitly. */
static int guest_sigh[32];
static volatile sig_atomic_t pending_sig;
static long guest_sigmask;	/* sigblock/sigsetmask: bit (s-1) blocks s */
static int  guest_reliable[32];	/* installed via sigvec/sigaction (4.3 frame) */
static long guest_hmask[32];	/* per-handler mask to add while it runs */
static int  guest_sigtramp;	/* the libc sigtramp address (passed to sigvec) */
/* Berkeley guest signal number -> Linux host signal (0 = no equivalent).
 * 1-15 are the shared V7 set; 16-31 the Berkeley job-control/extension
 * set (identical in 2.9/2.10/2.11 signal.h), where Linux renumbered:
 * guest STOP 17/TSTP 18/CONT 19/CHLD 20 vs host 19/20/18/17. */
static int sig_g2h(int g){
	switch(g){
	case 1: return SIGHUP;  case 2: return SIGINT;  case 3: return SIGQUIT;
	case 4: return SIGILL;  case 5: return SIGTRAP; case 6: return SIGABRT;	/* IOT */
	case 7: return 0;	/* EMT -- no Linux equivalent (raised internally) */
	case 8: return SIGFPE;  case 9: return SIGKILL; case 10: return SIGBUS;
	case 11: return SIGSEGV; case 12: return SIGSYS; case 13: return SIGPIPE;
	case 14: return SIGALRM; case 15: return SIGTERM;
	case 16: return SIGURG;  case 17: return SIGSTOP; case 18: return SIGTSTP;
	case 19: return SIGCONT; case 20: return SIGCHLD; case 21: return SIGTTIN;
	case 22: return SIGTTOU; case 23: return SIGIO;   case 24: return SIGXCPU;
	case 25: return SIGXFSZ; case 26: return SIGVTALRM; case 27: return SIGPROF;
	case 28: return SIGWINCH; case 30: return SIGUSR1; case 31: return SIGUSR2;
	default: return 0;
	}
}
/* Linux host signal -> guest signal number (0 = not forwarded) */
static int sig_h2g(int h){
	if(h==SIGHUP)return 1;  if(h==SIGINT)return 2;  if(h==SIGQUIT)return 3;
	if(h==SIGILL)return 4;  if(h==SIGTRAP)return 5; if(h==SIGABRT)return 6;
	if(h==SIGFPE)return 8;  if(h==SIGKILL)return 9; if(h==SIGBUS)return 10;
	if(h==SIGSEGV)return 11; if(h==SIGSYS)return 12; if(h==SIGPIPE)return 13;
	if(h==SIGALRM)return 14; if(h==SIGTERM)return 15;
	if(h==SIGURG)return 16;  if(h==SIGSTOP)return 17; if(h==SIGTSTP)return 18;
	if(h==SIGCONT)return 19; if(h==SIGCHLD)return 20; if(h==SIGTTIN)return 21;
	if(h==SIGTTOU)return 22; if(h==SIGIO)return 23;   if(h==SIGXCPU)return 24;
	if(h==SIGXFSZ)return 25; if(h==SIGVTALRM)return 26; if(h==SIGPROF)return 27;
	if(h==SIGWINCH)return 28; if(h==SIGUSR1)return 30; if(h==SIGUSR2)return 31;
	return 0;
}
/* What SIG_DFL does, per signal: 0 = terminate, 1 = ignore, 2 = stop.
 * A default stop is realized as a REAL host stop -- the guest process IS
 * a host process, so csh's SIGCONT genuinely resumes it. */
static int sig_dfl_action(int s){
	if(s==17||s==18||s==21||s==22) return 2;	/* STOP TSTP TTIN TTOU */
	if(s==16||s==19||s==20||s==23||s==28) return 1;	/* URG CONT CHLD IO WINCH */
	return 0;
}
/* Guest pid <-> host pid.  Guest pids are 15-bit; host pids on modern
 * Linux exceed that, so kill/setpgrp/wait arguments must round-trip
 * through a small map of the processes this one knows about (self,
 * parent, forked children).  An unknown value passes through. */
static struct { int g, h; } pidmap[64];
static void pid_learn(int h){
	int g=h&0x7fff, i;
	for(i=0;i<64;i++) if(pidmap[i].h==h) return;
	for(i=0;i<64;i++) if(!pidmap[i].h){ pidmap[i].g=g; pidmap[i].h=h; return; }
}
static int pid_g2h(int g){
	int i;
	if(g==0) return 0;
	for(i=0;i<64;i++) if(pidmap[i].h && pidmap[i].g==(g&0x7fff)) return pidmap[i].h;
	return g;
}
static void hsig(int s){ int g=sig_h2g(s); if(g) pending_sig=g; }
/* Raise a guest signal s (2.8 number) from emulated hardware (illegal instr,
 * BPT, IOT, EMT, odd-address).  Returns 1 if the guest has a handler (the main
 * loop will deliver it); 0 if the disposition is default -- the caller should
 * report the fault and halt, so genuine emulation gaps still surface. */
static int raise_fault(int s){
	if(s>0 && s<32 && guest_sigh[s]>1){ pending_sig=s; return 1; }
	return 0;
}

/* ---- FP11 floating-point unit ---------------------------------------
 * TRUE D-format soft arithmetic (not host doubles): a real PDP-11 -- FP11
 * hardware and the 2.8 fpsim interpreter alike -- carries a 56-bit mantissa
 * in D mode (24 in F mode) and rounds every operation by adding one to the
 * most significant discarded bit (no sticky, no round-to-even).  A host
 * double has only 53 mantissa bits, so emulating with host FP silently
 * mis-rounds the last bits (found via ecvt.c's .03: ...c3 on the real
 * machine, ...c0 under host-double emulation).  Accumulators are kept as
 * {sign, exp, frac}: value = 0.1fff... * 2^exp, frac normalized with its
 * MSB at bit 55; frac==0 means zero.  c1 runs everything in D mode, so the
 * FPS defaults to double precision. */
struct fpv { int sign; int exp; unsigned long long frac; };
static struct fpv AC[6];
static int FPS = 0200;			/* bit 0200 = double mode, 0100 = long int */
#define FPD 0200
#define FPL 0100
static int fN, fZ, fV, fC;		/* FP condition codes (cfcc copies to CPU) */

/* ---- First Edition (V1/V2, magic 0405) personality --------------------
 * Selected automatically by the magic: exec loads the WHOLE file (header
 * included) at core 040000 and jumps to it -- the magic word 0405 IS the
 * instruction `br .+14', branching over its own 12-byte header.  Syscalls
 * follow the 1971-72 convention (inline argument words after the trap,
 * fd-style first args in r0, C bit = error) and the machine grows a
 * KE11-A extended arithmetic element at 0177300 (V1 userland multiplies
 * and divides through it -- there is no EIS on the 11/20). */
static int v1sys;			/* 0405 loaded: V1 traps + KE11-A live */
static int KAC, KMQ;			/* KE11-A AC / MQ (16-bit values) */
#define KE11LO 0177300
#define KE11HI 0177316
static int ke11_rd(int a){
	switch(a&~1){
	case 0177302: return KAC&0xffff;		/* AC */
	case 0177304: return KMQ&0xffff;		/* MQ */
	default: return 0;		/* DIV/MUL/SC/SR/NOR/LSH/ASH read as 0 */
	}
}
static void ke11_wr(int a, int v){
	long dividend, product; int divisor;
	switch(a&~1){
	case 0177300:					/* DIV: (AC:MQ)/v */
		dividend = ((long)(KAC&0xffff)<<16) | (KMQ&0xffff);
		divisor = (short)v;
		if(divisor==0){ KMQ=0; KAC=0; return; }	/* real KE11 gives garbage */
		KMQ = (int)(dividend/divisor)&0xffff;
		KAC = (int)(dividend%divisor)&0xffff;
		return;
	case 0177302: KAC = v&0xffff; return;		/* AC */
	case 0177304: KMQ = v&0xffff;			/* MQ: sign extends into AC */
		KAC = (KMQ&0100000) ? 0xffff : 0; return;
	case 0177306:					/* MUL: MQ*v -> AC:MQ */
		product = (long)(short)(KMQ&0xffff) * (long)(short)v;
		KMQ = (int)(product&0xffff);
		KAC = (int)((product>>16)&0xffff);
		return;
	default: return;				/* SC/SR/NOR/LSH/ASH: unused by the corpus */
	}
}

/* Instruction-stream fetch: the opcode and any PC-following words (immediates,
 * index displacements, absolute-address words) come from I-space.  On a 0411
 * separate-I&D binary that is a distinct 64 KB from data; on 0407/0410 it
 * aliases the single shared space, so this is identical to ld2 there. */
static int ifetch(int a){ a&=0xffff; return Isp[a] | (Isp[(a+1)&0xffff]<<8); }
static int ld2(int a){ a&=0xffff;
	if(v1sys && a>=KE11LO && a<=KE11HI+1) return ke11_rd(a);
	return M[a] | (M[(a+1)&0xffff]<<8); }
static unsigned short pcring[16]; static int pcri; static int steppc;
static int rndpc=-2, rndn;	/* rnd-trace: log each call at PC=$APSIM_RNDPC (octal) */
static void st2(int a,int v){ a&=0xffff;
	if(v1sys && a>=KE11LO && a<=KE11HI+1){ ke11_wr(a,v&0xffff); return; }
	if(a==watchaddr || a+1==watchaddr) fprintf(stderr,"apsim: WATCH addr=%06o val=%06o steppc=%06o\n",a,v&0xffff,steppc);
	if(watchval && (v&0xffff)>=watchval && (v&0xffff)<=watchval+6) fprintf(stderr,"apsim: VALSET addr=%06o val=%06o steppc=%06o r2=%06o\n",a,v&0xffff,steppc,R[2]);
	if(watchtext && a<tsizew){ int k;
		fprintf(stderr,"apsim: TEXT WRITE addr=%06o val=%06o steppc=%06o instr=%06o\n",a,v&0xffff,steppc,ld2(steppc));
		fprintf(stderr,"  R0-5: %06o %06o %06o %06o %06o %06o SP=%06o\n",R[0],R[1],R[2],R[3],R[4],R[5],R[6]);
		fprintf(stderr,"  recent pcs:"); for(k=0;k<16;k++) fprintf(stderr," %06o",pcring[(pcri+k)&15]); fprintf(stderr,"\n");
		if(--watchtext==0) { halted=1; ecode=126; } }
	M[a]=v&0xff; M[(a+1)&0xffff]=(v>>8)&0xff; }
static int ld1(int a){ a&=0xffff;
	if(v1sys && a>=KE11LO && a<=KE11HI+1) return ke11_rd(a)&0xff;
	return M[a]; }
static void st1(int a,int v){ a&=0xffff;
	if(v1sys && a>=KE11LO && a<=KE11HI+1){ ke11_wr(a,v&0xff); return; }
	if(a==watchaddr){ fprintf(stderr,"apsim: WATCH1 addr=%06o val=%03o steppc=%06o r2=%06o iob[r2]: ptr=%06o cnt=%06o base=%06o flag=%06o\n",a,v&0xff,steppc,R[2],ld2(R[2]),ld2(R[2]+2),ld2(R[2]+4),ld2(R[2]+6)); }
	if(watchtext && a<tsizew){ fprintf(stderr,"apsim: TEXT WRITE1 addr=%06o val=%03o steppc=%06o instr=%06o\n",a,v&0xff,steppc,ld2(steppc));
		if(--watchtext==0) { halted=1; ecode=126; } }
	M[a]=v&0xff; }

/* DEC bit field (64-bit, F left-justified) <-> struct fpv.  Exact both ways.
 * value = (-1)^S * (0.5 + frac/2^56) * 2^(E-128), 8-bit excess-128 exponent. */
static struct fpv bits2fpv(unsigned long long bits){
	struct fpv r; int e=(bits>>55)&0377;
	if(e==0){ r.sign=0; r.exp=0; r.frac=0; return r; }	/* DEC zero */
	r.sign=(bits>>63)&1;
	r.exp=e-128;
	r.frac=(1ULL<<55)|(bits&0x7FFFFFFFFFFFFFULL);
	return r;
}
static unsigned long long fpv2bits(struct fpv v){
	int e;
	if(v.frac==0) return 0;
	e=v.exp+128;
	if(e<=0) return 0;				/* underflow -> DEC zero */
	if(e>0377) e=0377;				/* overflow -> clamp */
	return ((unsigned long long)(v.sign&1)<<63)|((unsigned long long)e<<55)
	      |(v.frac&0x7FFFFFFFFFFFFFULL);
}
static struct fpv rdfloat(int addr, int dbl){
	unsigned long long bits=0; int i, nw=dbl?4:2;
	for(i=0;i<nw;i++) bits=(bits<<16)|ld2((addr+2*i)&0xffff);
	if(!dbl) bits<<=32;			/* left-justify F into 64 bits */
	return bits2fpv(bits);
}
static void wrfloat(int addr, struct fpv v, int dbl){
	unsigned long long bits=fpv2bits(v); int i, nw=dbl?4:2;
	for(i=0;i<nw;i++) st2((addr+2*i)&0xffff, (bits>>(16*(3-i)))&0xFFFF);
}

/* ---- soft D/F arithmetic --------------------------------------------
 * Normalize a 128-bit intermediate (value = v * 2^(exp-120), i.e. bit 119
 * is the fraction-MSB position) and round to `bits' mantissa bits by adding
 * one at the first discarded bit -- the FP11 (and fpsim) rounding rule. */
typedef unsigned __int128 u128fp;
static struct fpv fp_norm128(int sign, int exp, u128fp v, int bits){
	struct fpv r;
	unsigned long long frac;
	int sh;
	if(v==0){ r.sign=0; r.exp=0; r.frac=0; return r; }
	while(v>>120){ v>>=1; exp++; }
	while(!(v>>119)){ v<<=1; exp--; }
	sh=120-bits;
	frac=(unsigned long long)(v>>sh);
	if((unsigned long long)(v>>(sh-1))&1){		/* first discarded bit */
		frac++;
		if(frac>>bits){ frac>>=1; exp++; }
	}
	r.sign=sign; r.exp=exp; r.frac=frac<<(56-bits);
	return r;
}
static int fpwidth(void){ return (FPS&FPD)?56:24; }
static struct fpv fp_zero(void){ struct fpv z; z.sign=0; z.exp=0; z.frac=0; return z; }
static struct fpv fp_addv(struct fpv a, struct fpv b){
	u128fp ma, mb;
	int d;
	if(a.frac==0) return b;
	if(b.frac==0) return a;
	if(a.exp<b.exp || (a.exp==b.exp && a.frac<b.frac)){ struct fpv t=a; a=b; b=t; }
	ma=(u128fp)a.frac<<64;
	d=a.exp-b.exp;
	mb=(d>63)?0:((u128fp)b.frac<<64)>>d;
	if(a.sign==b.sign)
		ma+=mb;
	else{
		ma-=mb;
		if(ma==0) return fp_zero();
	}
	return fp_norm128(a.sign, a.exp, ma, fpwidth());
}
static struct fpv fp_negv(struct fpv a){ if(a.frac) a.sign^=1; return a; }
static struct fpv fp_mulv(struct fpv a, struct fpv b){
	if(a.frac==0||b.frac==0) return fp_zero();
	return fp_norm128(a.sign^b.sign, a.exp+b.exp, (u128fp)a.frac*b.frac<<8, fpwidth());
}
static struct fpv fp_divv(struct fpv a, struct fpv b){
	if(a.frac==0) return fp_zero();
	if(b.frac==0) return fp_zero();			/* div by zero: FP11 traps; give 0 */
	return fp_norm128(a.sign^b.sign, a.exp-b.exp,
			  (((u128fp)a.frac<<64)/b.frac)<<56, fpwidth());
}
static int fp_cmpv(struct fpv a, struct fpv b){		/* sign of (a-b), exact */
	if(a.frac==0 && b.frac==0) return 0;
	if(a.frac==0) return b.sign?1:-1;
	if(b.frac==0) return a.sign?-1:1;
	if(a.sign!=b.sign) return a.sign?-1:1;
	{ int m=1; if(a.sign) m=-1;
	  if(a.exp!=b.exp) return (a.exp>b.exp)?m:-m;
	  if(a.frac!=b.frac) return (a.frac>b.frac)?m:-m;
	  return 0; }
}
static struct fpv fp_fromlong(long v){			/* rounds at mode width */
	int sign=0;
	if(v==0) return fp_zero();
	if(v<0){ sign=1; v=-v; }
	return fp_norm128(sign, 56, (u128fp)(unsigned long long)v<<64, fpwidth());
}
static long fp_tolong(struct fpv v){			/* truncate toward zero */
	unsigned long long iv;
	if(v.frac==0 || v.exp<=0) return 0;
	if(v.exp>=56) iv=v.frac<<(v.exp-56>8?8:v.exp-56);	/* overflow: clamp-ish */
	else iv=v.frac>>(56-v.exp);
	return v.sign?-(long)iv:(long)iv;
}

/* ---- operand resolution ---------------------------------------------
 * Resolve a 6-bit mode|reg field to a "location": a register (ISREG|n) or
 * a 16-bit memory address.  Auto-inc/dec and index words advance as needed.
 */
#define ISREG 0x10000
#define ISIMM 0x20000			/* operand is an immediate literal (held in immv) */
static int immv;			/* value of the last #immediate (an I-space literal) */
static int operand(int spec, int byte)
{
	int mode=(spec>>3)&7, rn=spec&7;
	int inc=(byte && rn<6)?1:2, a, x;
	switch(mode){
	case 0: return ISREG|rn;				/* Rn       */
	case 1: return R[rn];					/* (Rn)     */
	case 2:
		if(rn==7){ immv=ifetch(PC); PC=(PC+2)&0xffff; return ISIMM; }	/* #imm  (I-space literal) */
		a=R[rn]; R[rn]=(R[rn]+inc)&0xffff; return a;	/* (Rn)+    */
	case 3:
		if(rn==7){ a=ifetch(PC); PC=(PC+2)&0xffff; return a; }	/* *$abs (addr word I-space; data D-space) */
		a=ld2(R[rn]); R[rn]=(R[rn]+2)&0xffff; return a;	/* @(Rn)+  (pointer in D-space) */
	case 4: R[rn]=(R[rn]-inc)&0xffff; return R[rn];		/* -(Rn)    */
	case 5: R[rn]=(R[rn]-2)&0xffff; return ld2(R[rn]);	/* @-(Rn)  (pointer in D-space) */
	case 6: x=ifetch(PC); PC=(PC+2)&0xffff; return (R[rn]+x)&0xffff;		/* X(Rn)  (X from I-space) */
	case 7: x=ifetch(PC); PC=(PC+2)&0xffff; return ld2((R[rn]+x)&0xffff);	/* @X(Rn) (X from I; ptr in D) */
	}
	return 0;
}
static int getv(int loc, int byte){
	if(loc&ISIMM){ return byte?(immv&0xff):(immv&0xffff); }
	if(loc&ISREG){ int v=R[loc&7]; return byte?(v&0xff):v; }
	return byte?ld1(loc):ld2(loc);
}
static void putv(int loc, int v, int byte){
	if(loc&ISIMM) return;			/* a literal can't be a destination */
	if(loc&ISREG){
		if(byte) R[loc&7]=(R[loc&7]&0xff00)|(v&0xff);
		else R[loc&7]=v&0xffff;
		return;
	}
	if(byte) st1(loc,v); else st2(loc,v);
}
static int sgn(int v,int byte){ return byte?(signed char)v:(signed short)v; }
static void setNZ(int v,int byte){ FZ=((byte?v&0xff:v&0xffff)==0); FN=(sgn(v,byte)<0); }

/* ---- 2BSD sys-call emulation ---------------------------------------- */
static char *mappath(int gaddr);
static int load_aout(const char *path, int nargs, char **args);
static int load_aout_env(const char *path, int nargs, char **args, int nenv, char **env);
/* fill a guest `sgttyb' (6 bytes) at byte address `buf' from our emulated tty;
 * shared by gtty(32), the old stty(31) readback, and ioctl TIOCGETP. */
static void sgtty_get(int buf){
	st1(buf+0,13); st1(buf+1,13);		/* i/ospeed = B9600 */
	st1(buf+2,0177); st1(buf+3,025);	/* erase=DEL, kill=^U */
	st2(buf+4,guest_sgflags);
}

/* ---- per-era struct stat writers -------------------------------------
 * One function per byte layout, selected by personality (the VAX apsim
 * rule: a remap moves a number, never a struct shape).  PDP-11 longs are
 * written high word first.  File times are real host times, or the
 * pinned $APSIM_TIME when set (so byte-compared runs stay deterministic). */
static long stat_time(time_t t){
	char *e=getenv("APSIM_TIME");
	return e ? atol(e) : (long)t;
}
static void stl(int a, long v){ st2(a,(int)((v>>16)&0xffff)); st2(a+2,(int)(v&0xffff)); }
/* V7/2.8/2.9 (and sys3/Ultrix-11): 30 bytes -- 7 shorts, then size and
 * three times as longs. */
static void put_stat_v7(int sb, struct stat *hs){
	st2(sb+0,hs->st_dev); st2(sb+2,hs->st_ino&0xffff); st2(sb+4,hs->st_mode);
	st2(sb+6,hs->st_nlink); st2(sb+8,hs->st_uid); st2(sb+10,hs->st_gid);
	st2(sb+12,hs->st_rdev);
	stl(sb+14,(long)(hs->st_size>0xffffff?0xffffff:hs->st_size));
	stl(sb+18,stat_time(hs->st_atime));
	stl(sb+22,stat_time(hs->st_mtime));
	stl(sb+26,stat_time(hs->st_ctime));
}
/* V5/V6: 36 bytes, the in-core inode image (kernel stat1 copies 14 words
 * from &i_dev, then 4 time words off the raw disk inode): dev, ino, mode,
 * then PACKED BYTES nlink/uid/gid/size0, size1, addr[8], atime, mtime. */
static void put_stat_v6(int sb, struct stat *hs){
	int k; long sz = hs->st_size>0xffffff ? 0xffffff : (long)hs->st_size;
	int mode = 0100000 /* IALLOC */
		| (S_ISDIR(hs->st_mode)?040000:0)
		| (S_ISCHR(hs->st_mode)?020000:0)
		| (S_ISBLK(hs->st_mode)?060000:0)
		| (sz>4095?010000:0)		/* ILARG */
		| (hs->st_mode&07777);
	st2(sb+0,hs->st_dev); st2(sb+2,hs->st_ino&0xffff); st2(sb+4,mode);
	st1(sb+6,hs->st_nlink&0xff); st1(sb+7,hs->st_uid&0xff);
	st1(sb+8,hs->st_gid&0xff); st1(sb+9,(int)((sz>>16)&0xff));
	st2(sb+10,(int)(sz&0xffff));
	for(k=0;k<8;k++) st2(sb+12+2*k,0);	/* i_addr[8] */
	stl(sb+28,stat_time(hs->st_atime));
	stl(sb+32,stat_time(hs->st_mtime));
}
/* 2.10/2.11: the 4.3-shape stat, 52 BYTES on the PDP-11.  The st_spare1-3
 * fields between the times are `int' = 2 bytes here (they are 4 on the
 * VAX -- assuming the VAX width overruns the guest's 52-byte buffer by 6
 * and smashes the caller's adjacent frame locals: ls's DIR *dirp,
 * _flsbuf's state, date's frame chain.  That overflow was this
 * emulator's hardest bug; measure, don't assume).  Tail: 2.11 has
 * u_short st_flags + spare[3], 2.10 long spare4[2] -- same 8 bytes. */
static void put_stat_211(int sb, struct stat *hs){
	st2(sb+0,hs->st_dev); st2(sb+2,hs->st_ino&0xffff); st2(sb+4,hs->st_mode);
	st2(sb+6,hs->st_nlink); st2(sb+8,hs->st_uid); st2(sb+10,hs->st_gid);
	st2(sb+12,hs->st_rdev);
	stl(sb+14,(long)(hs->st_size>0xffffff?0xffffff:hs->st_size));
	stl(sb+18,stat_time(hs->st_atime)); st2(sb+22,0);	/* int spare1 */
	stl(sb+24,stat_time(hs->st_mtime)); st2(sb+28,0);	/* int spare2 */
	stl(sb+30,stat_time(hs->st_ctime)); st2(sb+34,0);	/* int spare3 */
	stl(sb+36,1024);			/* st_blksize */
	stl(sb+40,(long)((hs->st_size+1023)/1024));
	st2(sb+44,0); st2(sb+46,0); st2(sb+48,0); st2(sb+50,0);
}
/* era dispatch: stat211 is set when the number arrived through a 2.10/2.11
 * remap entry flagged SR_STAT (the "new" stat trio); the compat "old stat"
 * numbers keep the V7 shape even in those universes. */
static void put_stat(int sb, struct stat *hs, int stat211){
	if(stat211) put_stat_211(sb,hs);
	else if(Kern==PDP11_K_V56) put_stat_v6(sb,hs);
	else put_stat_v7(sb,hs);
}

/* ---- directory snapshots ---------------------------------------------
 * Classic UNIX lists a directory by read(2)ing it; Linux refuses that.
 * When the guest opens a directory, build a snapshot in the era's on-disk
 * record format and serve read/lseek from it (the VAX apsim's dir_build).
 * Guest fds are host fds, so the snapshot is keyed by fd. */
#define NGDIR 16
static struct gdir { int fd; unsigned char *buf; int len, pos; } gdirs[NGDIR];
static struct gdir *dir_find(int fd){
	int i;
	if(fd<0) return 0;
	for(i=0;i<NGDIR;i++) if(gdirs[i].buf && gdirs[i].fd==fd) return &gdirs[i];
	return 0;
}
static void dir_drop(int fd){
	struct gdir *g=dir_find(fd);
	if(g){ free(g->buf); g->buf=0; }
}
/* build a directory snapshot in the era's format; returns malloc'd buffer
 * and its length, or NULL.  Callers: dir_snapshot (registers it for a fd)
 * and dir_guest_size (guest stat of a directory must report the SNAPSHOT
 * length as st_size -- 2.11's opendir sizes its buffers from st_size, and
 * a host size over a differently-sized snapshot makes readdir walk
 * entries that do not exist). */
static unsigned char *dir_build(const char *hpath, int *lenp){
	DIR *d; struct dirent *e;
	unsigned char *buf; int cap=4096, len=0, lastrec=-1;
	if(!(d=opendir(hpath))) return 0;
	buf=malloc(cap);
	while((e=readdir(d))){
		int ino=(int)(e->d_ino&0xffff); if(ino==0) ino=1;  /* 0 = free slot */
		while(len+600>cap){ cap*=2; buf=realloc(buf,cap); }
		if(Kern==PDP11_K_V1){		/* 10-byte: ino + name[8] */
			int k; buf[len]=ino&0xff; buf[len+1]=(ino>>8)&0xff;
			for(k=0;k<8;k++) buf[len+2+k]= k<(int)strlen(e->d_name)?e->d_name[k]:0;
			len+=10;
		} else if(Kern>=PDP11_K_BSD210){
			/* 4.3-style variable records packed into 512-byte blocks:
			 * {d_ino, d_reclen, d_namlen, name NUL-padded to a 4-byte
			 * boundary}.  A record never crosses a block; remaining
			 * room too small for the next entry is closed out with a
			 * SEPARATE empty record (ino 0, reclen = what's left) --
			 * the same shape Apout emits -- rather than by inflating
			 * the last real entry's reclen. */
			int nl=(int)strlen(e->d_name); if(nl>63) nl=63;
			int rl=((6+nl+1)+3)&~3;
			int left=512-(len&511);
			if(rl+10 > left){
				buf[len]=0; buf[len+1]=0;	/* empty pad record */
				buf[len+2]=left&0xff; buf[len+3]=(left>>8)&0xff;
				buf[len+4]=0; buf[len+5]=0;
				memset(buf+len+6,0,left-6);
				len+=left;
			}
			buf[len]=ino&0xff; buf[len+1]=(ino>>8)&0xff;
			buf[len+2]=rl&0xff; buf[len+3]=(rl>>8)&0xff;
			buf[len+4]=nl&0xff; buf[len+5]=(nl>>8)&0xff;
			memset(buf+len+6,0,rl-6);
			memcpy(buf+len+6,e->d_name,nl);
			lastrec=len; len+=rl;
		} else {			/* V5..2.9: 16-byte ino + name[14] */
			int k; buf[len]=ino&0xff; buf[len+1]=(ino>>8)&0xff;
			for(k=0;k<14;k++) buf[len+2+k]= k<(int)strlen(e->d_name)?e->d_name[k]:0;
			len+=16;
		}
	}
	if(Kern>=PDP11_K_BSD210 && (len&511)!=0){
		/* close out the final block with an empty record */
		int left=512-(len&511);
		buf[len]=0; buf[len+1]=0;
		buf[len+2]=left&0xff; buf[len+3]=(left>>8)&0xff;
		buf[len+4]=0; buf[len+5]=0;
		memset(buf+len+6,0,left-6);
		len+=left;
	}
	closedir(d);
	*lenp=len;
	return buf;
}
static void dir_snapshot(int fd, const char *hpath){
	struct gdir *g=0; int i, len;
	unsigned char *buf;
	for(i=0;i<NGDIR;i++) if(!gdirs[i].buf){ g=&gdirs[i]; break; }
	if(!g) return;				/* too many open dirs: reads give EISDIR */
	if(!(buf=dir_build(hpath,&len))) return;
	g->fd=fd; g->buf=buf; g->len=len; g->pos=0;
}
static long dir_guest_size(const char *hpath){
	int len; unsigned char *buf=dir_build(hpath,&len);
	if(!buf) return 0;
	free(buf);
	return len;
}
/* ---- per-era syscall renumbering -------------------------------------
 * The switch below is the CANONICAL table: V7 numbering (the lineage
 * trunk, which 2.8/2.9 extend in place) plus synthetic extension ids for
 * calls with no V7 ancestor.  A renumbering era gets one data table
 * mapping its guest numbers onto canonical ones; a remap entry moves a
 * number, never a struct shape -- shape travels in the SR_STAT flag
 * (2.10/2.11's "new" stat trio vs their compat "old stat" numbers). */
#define SR_STAT 1
struct sremap { unsigned short guest, canon, flags; };
#define R_(g,c)  { (g), (c), 0 }
#define RS_(g,c) { (g), (c), SR_STAT }
#define CX(n) (0x100+(n))	/* canonical extension ids (not V7 numbers) */
enum {
	C_LSTAT=CX(1), C_GETPAGESIZE, C_SBRK, C_DUP2, C_GETDTABLESIZE,
	C_GETTIMEOFDAY, C_SELECT, C_FCNTL, C_MKDIR, C_RMDIR, C_RENAME,
	C_TRUNCATE, C_FTRUNCATE, C_GETPGRP, C_GETHOSTNAME, C_READV,
	C_WRITEV, C_SYMLINK, C_READLINK, C_GETRUSAGE, C_UTIMES, C_WAIT4,
	C_GETPPID, C_GETEUID, C_GETEGID, C_SIGVEC, C_GETGROUPS,
	C_GETRLIMIT, C_KILLPG, C_FCHDIR, C_GETLOGIN, C_UNAME, C_ULIMIT,
	C_NAP, C_FCHMOD, C_FSYNC, C_SYSCTL, C_SETPGRP, C_SIGBLOCK,
	C_SIGSETMASK, C_SIGSUSPEND, C_SIGRETURN, C_OK, C_NOSYS
};
/* 2.10BSD: the 4.3BSD numbering; low numbers are 4.3's compat "old" set
 * and pass through with V7 shapes. */
static const struct sremap Bsd210Remap[] = {
	RS_(38,18), RS_(40,C_LSTAT), RS_(62,28),
	{57,C_SYMLINK,0}, {58,C_READLINK,0}, {64,C_GETPAGESIZE,0},
	{69,C_SBRK,0}, {79,C_GETGROUPS,0}, {80,C_OK,0}, {81,C_GETPGRP,0},
	{82,C_SETPGRP,0}, {83,C_OK,0}, {84,7,0}, {86,C_OK,0},
	{87,C_GETHOSTNAME,0}, {88,C_OK,0}, {89,C_GETDTABLESIZE,0},
	{90,C_DUP2,0}, {92,C_FCNTL,0}, {93,C_SELECT,0}, {95,C_FSYNC,0},
	{96,C_OK,0}, {100,C_OK,0}, {103,C_SIGRETURN,0}, {108,C_SIGVEC,0},
	{109,C_SIGBLOCK,0}, {110,C_SIGSETMASK,0}, {111,29,0}, {112,C_OK,0},
	{116,C_GETTIMEOFDAY,0}, {117,C_GETRUSAGE,0}, {120,C_READV,0},
	{121,C_WRITEV,0}, {122,C_OK,0}, {123,C_OK,0}, {124,C_FCHMOD,0},
	{128,C_RENAME,0}, {129,C_TRUNCATE,0}, {130,C_FTRUNCATE,0},
	{131,C_OK,0}, {136,C_MKDIR,0}, {137,C_RMDIR,0}, {138,C_UTIMES,0},
	{144,C_GETRLIMIT,0}, {145,C_OK,0}, {146,C_KILLPG,0},
	{ 0, 0, 0 }
};
/* 2.11BSD pl431: the 4.4-style renumbered table (wait4=7, sigaction=31,
 * getppid=27, the id calls resorted). */
static const struct sremap Bsd211Remap[] = {
	{7,C_WAIT4,0}, {13,C_FCHDIR,0}, {17,C_OK,0}, {18,C_OK,0},
	{23,C_SYSCTL,0}, {25,C_GETEUID,0}, {27,C_GETPPID,0},
	{28,C_NOSYS,0}, {29,C_NOSYS,0}, {30,C_NOSYS,0} /* statfs trio */,
	{31,C_SIGVEC,0} /* sigaction: same record-the-handler treatment */,
	{32,C_OK,0}, {34,C_OK,0}, {35,C_OK,0},
	RS_(38,18), {39,C_GETLOGIN,0}, RS_(40,C_LSTAT), {43,C_OK,0},
	{45,23,0} /* setuid */, {46,C_OK,0} /* seteuid */,
	{48,C_GETEGID,0}, {49,46,0} /* setgid */, {50,C_OK,0},
	{56,C_NOSYS,0}, {57,C_SYMLINK,0}, {58,C_READLINK,0},
	RS_(62,28), {64,C_GETPAGESIZE,0}, {65,C_NOSYS,0} /* pselect */,
	{66,2,0} /* vfork -> fork */, {69,C_SBRK,0},
	{79,C_GETGROUPS,0}, {80,C_OK,0}, {81,C_GETPGRP,0}, {82,C_SETPGRP,0},
	{83,C_OK,0}, {86,C_OK,0}, {87,C_GETHOSTNAME,0}, {88,C_OK,0},
	{89,C_GETDTABLESIZE,0}, {90,C_DUP2,0}, {92,C_FCNTL,0},
	{93,C_SELECT,0}, {95,C_FSYNC,0}, {96,C_OK,0}, {100,C_OK,0},
	{103,C_SIGRETURN,0}, {107,C_SIGSUSPEND,0} /* pl431 sigsuspend */,
	{108,C_SIGVEC,0}, {109,C_SIGBLOCK,0}, {110,C_SIGSETMASK,0},
	{111,29,0}, {112,C_OK,0}, {116,C_GETTIMEOFDAY,0},
	{117,C_GETRUSAGE,0}, {120,C_READV,0}, {121,C_WRITEV,0},
	{122,C_OK,0}, {123,C_OK,0}, {124,C_FCHMOD,0}, {128,C_RENAME,0},
	{129,C_TRUNCATE,0}, {130,C_FTRUNCATE,0}, {131,C_OK,0},
	{136,C_MKDIR,0}, {137,C_RMDIR,0}, {138,C_UTIMES,0},
	{144,C_GETRLIMIT,0}, {145,C_OK,0}, {146,C_KILLPG,0}, {148,C_OK,0},
	{ 0, 0, 0 }
};
/* System III / Ultrix-11: V7 base + the SysIII additions DEC carried
 * (utssys, ulimit; Ultrix adds fcntl and its own housekeeping calls).
 * Note 57 collides with 2.9's vfork -- personality disambiguates. */
static const struct sremap Sys3Remap[] = {
	{57,C_UNAME,0} /* utssys */, {62,C_FCNTL,0}, {63,C_ULIMIT,0},
	{64,C_OK,0}, {68,C_OK,0}, {69,C_OK,0}, {71,C_NAP,0},
	{79,C_NOSYS,0} /* semsys */,
	{ 0, 0, 0 }
};
static int sremap_apply(const struct sremap *t, int code, int *stat211){
	for(; t->guest; t++)
		if(t->guest==code){
			if(t->flags & SR_STAT) *stat211=1;
			return t->canon;
		}
	return code;
}
/* V5/V6: numbers the era did not have (nosys slots in the sysent) */
static int v56_nosys(int n){
	if(n==27||n==29||n==33||n==39||n==40||n==45) return 1;
	if(n>=49 && n<=63) return 1;
	if(n==26 && Kern==PDP11_K_V56 && !strcmp(Univ,"v5")) return 1; /* v5: no ptrace */
	return 0;
}

static int TrapPC;	/* address of the current sys instruction: EINTR
			 * restart re-executes the trap after the handler
			 * returns (the kernel's ERESTART), so a SIGCHLD
			 * arriving while csh sits in read() doesn't surface
			 * as a spurious error */
static void do_syscall(int num, int argaddr)
{
	/* argaddr points just past the sys instruction's number word: the
	 * inline arguments (for indirect calls) live there.  fd-style first
	 * args are in R0 (the stubs put them there). */
	int a1 = ld2(argaddr), a2 = ld2(argaddr+2);
	int fd0 = R[0], a3 = ld2(argaddr+4);
	int stat211 = 0;
	long r;
	/* per-era renumbering: map the guest number onto the canonical table */
	if(Kern==PDP11_K_BSD210)      num = sremap_apply(Bsd210Remap, num, &stat211);
	else if(Kern==PDP11_K_BSD211) num = sremap_apply(Bsd211Remap, num, &stat211);
	else if(Kern==PDP11_K_SYS3 || Kern==PDP11_K_ULTRIX)
	                              num = sremap_apply(Sys3Remap, num, &stat211);
	else if(Kern==PDP11_K_V56 && v56_nosys(num)){
		if(systrace) fprintf(stderr, "sys %d: nosys in this era\n", num);
		FC=1; R[0]=EINVAL; return;
	}
	if(stackargs){
		int s1=ld2((SP+2)&0xffff), s2=ld2((SP+4)&0xffff),
		    s3=ld2((SP+6)&0xffff), s4=ld2((SP+8)&0xffff);
		switch(num){
		case 3: case 4: case 6: case 19: case 28: case 41: case 54:
		case C_DUP2: case C_FCNTL: case C_READV: case C_WRITEV:
		case C_FTRUNCATE: case C_FCHMOD: case C_FSYNC: case C_FCHDIR:
			/* first arg is an fd (V7 took it in r0) */
			fd0=s1; a1=s2; a2=s3; a3=s4; break;
		default:
			a1=s1; a2=s2; a3=s3; break;
		}
	}
	if(Kern==PDP11_K_BSD2X && num==57) num=2;	/* 2.9 vfork: treat as fork */
	if(systrace) fprintf(stderr, "[%d] sys %d fd0=%d a1=%06o a2=%06o a3=%06o\n", getpid()&0x7fff, num, fd0, a1, a2, a3);
	switch(num){
	case 1:				/* exit(code in r0; 2.10: on stack) */
		halted=1; ecode=(stackargs?a1:R[0])&0xff; return;
	case 2: {			/* fork: real host fork duplicates apsim's guest
					 * state (M/MI/R/...) for free.  V7 convention: the
					 * kernel returns to the PARENT past the `br' that
					 * follows `sys fork' (so PC+=2, r0=child pid), and to
					 * the CHILD at that `br' (r0=parent pid -> br 1f -> 0). */
		int pid = fork();
		if(pid < 0){ FC=1; R[0]=errno_h2g(errno); return; }
		if(pid > 0){ pid_learn(pid); FC=0; R[0]=pid&0x7fff; PC=(PC+2)&0xffff; return; }	/* parent */
		pid_learn(getpid()); pid_learn(getppid());
		FC=0; R[0]=getppid()&0x7fff; return;				/* child: r0=ppid */
	}
	case 7: {			/* wait: reap a child; r0=pid, r1=status word.
					 * wait3 rides the 4.2 convention: ALL FOUR
					 * condition codes set at the trap means
					 * options in r0 (WNOHANG=1, WUNTRACED=2) and
					 * &rusage in r1.  A stopped child reports
					 * status (stopsig<<8)|0177. */
		int st, opts=0, wpid;
		if(FC&&FV&&FZ&&FN) opts=((R[0]&1)?WNOHANG:0)|((R[0]&2)?WUNTRACED:0);
		wpid = waitpid(-1, &st, opts);
		if(wpid < 0){ FC=1; R[0]=errno_h2g(errno); return; }
		if(wpid == 0){ FC=0; R[0]=0; R[1]=0; return; }	/* WNOHANG: nothing */
		if(WIFSTOPPED(st))
			R[1] = ((sig_h2g(WSTOPSIG(st))<<8)|0177)&0xffff;
		else if(WIFEXITED(st))
			R[1] = ((WEXITSTATUS(st)&0xff)<<8)&0xffff;
		else
			R[1] = sig_h2g(WTERMSIG(st))&0x7f;
		FC=0; R[0]=wpid&0x7fff; return;
	}
	case 42: {			/* pipe: r0=read fd, r1=write fd (host fds, used
					 * directly by the guest). */
		int fd[2];
		if(pipe(fd) < 0){ FC=1; R[0]=errno_h2g(errno); return; }
		R[1]=fd[1]&0xffff; FC=0; R[0]=fd[0]&0xffff; return;
	}
	case 4:				/* write(r0=fd, a1=buf, a2=count) */
		r = write(fd0, M+(a1&0xffff), a2&0xffff);
		break;
	case 3: {			/* read(r0=fd, a1=buf, a2=count) */
		struct gdir *g = dir_find(fd0);
		if(g){			/* directory: serve the era-format snapshot */
			int n = a2&0xffff;
			if(n > g->len - g->pos) n = g->len - g->pos;
			if(n < 0) n = 0;
			memcpy(M+(a1&0xffff), g->buf+g->pos, n);
			g->pos += n; r = n; break;
		}
		r = read(fd0, M+(a1&0xffff), a2&0xffff);
		break;
	}
	case 5: {			/* open(a1=path, a2=mode) */
		char hp[1024]; struct stat ds;
		if(systrace) fprintf(stderr, "    open ret=%06o path='%s'\n", ld2(SP), (char*)(M+(a1&0xffff)));
		strncpy(hp, mappath(a1), sizeof hp-1); hp[sizeof hp-1]=0;
		r = open(hp, Kern>=PDP11_K_BSD210 ? (a2&3) : a2);
		/* classic UNIX lists directories by read(2); snapshot them */
		if(r>=0 && fstat((int)r,&ds)==0 && S_ISDIR(ds.st_mode))
			dir_snapshot((int)r, hp);
		break;
	}
	case 6:				/* close(r0=fd) */
		dir_drop(fd0);
		r = close(fd0);
		break;
	case 8:				/* creat(a1=path, a2=mode) */
		if(systrace) fprintf(stderr, "    creat path='%s' mode=%o\n", mappath(a1), a2);
		r = creat(mappath(a1), a2);
		break;
	case 19: {			/* V5/V6: seek(fd, offset16, ptrname 0-5);
					 * V7+: lseek(fd, long off, whence). */
		struct gdir *g = dir_find(fd0);
		if(Kern <= PDP11_K_V56){
			long off; int ptr = a2;
			off = (ptr==0) ? (long)(a1&0xffff) : (long)(short)a1;
			if(ptr>=3){ off*=512; ptr-=3; }	/* block modes */
			if(g){			/* directory snapshot position */
				g->pos = (ptr==0)?off : (ptr==1)?g->pos+off : g->len+off;
				if(g->pos<0) g->pos=0; r=0; break;
			}
			r = lseek(fd0, off, ptr);
			if(r>=0) r=0;
			break;
		}
		/* lseek: off is a long passed high-word-first (a1=high, a2=low),
		 * whence inline 3rd.  The RESULT is also a long, returned in the
		 * r0:r1 pair (r0=high, r1=low) -- like time(13); a single 16-bit
		 * return truncates ftell() and archive scans. */
		{
		long off = ((long)(a1&0xffff)<<16) | (a2&0xffff);
		int whence = stackargs?a3:ld2(argaddr+4);
		if(g){
			g->pos = (whence==0)?off : (whence==1)?g->pos+off : g->len+off;
			if(g->pos<0) g->pos=0;
			R[1] = g->pos & 0xffff; r = (g->pos>>16)&0xffff; break;
		}
		r = lseek(fd0, off, whence);
		if (r >= 0) { R[1] = r & 0xffff; r = (r >> 16) & 0xffff; }
		break;
		}
	}
	case 10:			/* unlink(a1=path) */
		r = unlink(mappath(a1));
		break;
	case 11: {			/* execv(a1=name, a2=argv): replace the image and
					 * continue at its entry; no return on success.  The
					 * 2-pass as uses this (pass1 execs as2). */
		static char argbuf[64][256]; char *args[64]; char hp[1024];
		int n=0, aptr=a2&0xffff, p;
		strncpy(hp, mappath(a1), sizeof hp -1); hp[sizeof hp-1]=0;
		while(n<64){ p=ld2(aptr); aptr+=2; if(p==0) break;
			strncpy(argbuf[n],(char*)(M+(p&0xffff)),255); argbuf[n][255]=0;
			args[n]=argbuf[n]; n++; }
		if(load_aout(hp, n, args)<0){ FC=1; R[0]=ENOENT; break; }
		return;			/* success: run the new program */
	}
	case 33:			/* access(a1=path, a2=mode) -- ld/openlp probes
					 * library files for existence/readability. */
		r = access(mappath(a1), a2);
		break;
	case 18: case 28: case C_LSTAT: {
					/* stat(a1=path,a2=buf) / fstat(r0=fd,a1=buf) /
					 * lstat (2.10/2.11, and 2.9's local #2): fill the
					 * era's struct stat shape (put_stat). */
		struct stat hs; int sb, ok;
		if(num==28){ ok=fstat(fd0,&hs); sb=a1&0xffff; }
		else if(num==C_LSTAT){ if(systrace) fprintf(stderr, "    lstat path='%s' -> %s\n", (char*)(M+(a1&0xffff)), mappath(a1)); ok=lstat(mappath(a1),&hs); sb=a2&0xffff; }
		else { if(systrace) fprintf(stderr, "    stat path='%s'\n", mappath(a1)); ok=stat(mappath(a1),&hs); sb=a2&0xffff; }
		if(ok<0){ r=-1; break; }
		if(S_ISDIR(hs.st_mode)){
			/* the guest sees the era-format snapshot, so its size
			 * (not the host directory's) is the truth -- opendir
			 * and getwd size their reads from st_size */
			struct gdir *g = (num==28) ? dir_find(fd0) : 0;
			if(g) hs.st_size = g->len;
			else hs.st_size = dir_guest_size(num==28 ? "." : mappath(a1));
		}
		put_stat(sb, &hs, stat211);
		r=0;
		break;
	}
	case 60:			/* umask(a1=mask) -- ld sets it before creat;
					 * return a typical old mask, no host effect. */
		r = 022;
		break;
	case 15:			/* chmod(a1=path, a2=mode) -- ld marks the
					 * output executable after writing it. */
		r = chmod(mappath(a1), a2);
		break;
	case 54: {			/* ioctl(fd, request, argp).  Inline form (V7..2.9):
					 * args are fd, request(16-bit), argp.  Stack form
					 * (2.10/2.11): request is a 32-bit 4.3-style _IO code
					 * pushed high word first -- a1=IOC bits|size, a2=the
					 * identifying ('t'<<8)|n low half -- and argp is a3.
					 * apsim presents ONE emulated terminal, so the tty
					 * ioctls succeed regardless of which fd; host termios
					 * is touched only when our real stdin is a tty. */
		int req = a2;	/* inline: the 16-bit request; stack: the low half of the 32-bit 4.3 code -- both land in a2 */
		int argp = (stackargs ? a3 : ld2(argaddr+4)) & 0xffff;
		/* tty ioctls only succeed on a real tty: isatty() is HOW guests
		 * decide between interactive and pipe behavior (2.11 ls picks
		 * its whole output strategy from it) -- unconditional success
		 * made every redirected fd look like a terminal. */
		if(!isatty(fd0)){ errno=ENOTTY; r=-1; break; }
		if(req == (('t'<<8)|119)){	/* TIOCGPGRP: the tty's real host
					 * process group, the other half of csh's
					 * job-control handshake */
			int pg = tcgetpgrp(fd0);
			if(pg<0){ r=-1; break; }
			st2(argp, pg&0x7fff); r=0; break;
		}
		if(req == (('t'<<8)|118)){	/* TIOCSPGRP: hand the terminal to a
					 * job's process group (host tcsetpgrp; the
					 * host kernel then does TTIN/TTOU/TSTP
					 * routing for us) */
			r = tcsetpgrp(fd0, pid_g2h(ld2(argp)));
			break;
		}
		if(req == (('t'<<8)|124)){	/* TIOCLGET: local mode word */
			st2(argp, guest_lmode); r=0; break;
		}
		if(req == (('t'<<8)|125)){ guest_lmode=ld2(argp); r=0; break; }  /* TIOCLSET */
		if(req == (('t'<<8)|126)){ guest_lmode&=~ld2(argp); r=0; break; } /* TIOCLBIC */
		if(req == (('t'<<8)|127)){ guest_lmode|=ld2(argp); r=0; break; }  /* TIOCLBIS */
		if(req == (('t'<<8)|104)){	/* TIOCGWINSZ: rows/cols/x/y --
					 * answer 24x80; leaving it unanswered gives
					 * column-layout code a 0-width loop (2.11 ls) */
			st2(argp+0,24); st2(argp+2,80); st2(argp+4,0); st2(argp+6,0);
			r=0; break;
		}
		switch(req){
		case ('t'<<8)|8:	/* TIOCGETP: fill sgttyb from our notion of the tty */
			sgtty_get(argp);
			r=0; break;
		case ('t'<<8)|9:	/* TIOCSETP (with flush) */
		case ('t'<<8)|10:	/* TIOCSETN (no flush) */
			tty_apply(ld2(argp+4)&0xffff);
			r=0; break;
		case ('t'<<8)|18:	/* TIOCGETC: tchars (intr,quit,start,stop,eof,brk) */
			st1(argp+0,03); st1(argp+1,034); st1(argp+2,021);
			st1(argp+3,023); st1(argp+4,04); st1(argp+5,0377);
			r=0; break;
		case ('t'<<8)|17:	/* TIOCSETC: accept, no host effect */
		case ('t'<<8)|0:	/* TIOCGETD: line discipline */
			if(req==(('t'<<8)|0)) st2(argp,0);
			r=0; break;
		default: r=0; break;		/* accept other tty ioctls quietly */
		}
		break;
	}
	case 48: {			/* signal(a1=sig, a2=handler): record the guest
					 * disposition and mirror it onto the host so the
					 * signal is actually caught/ignored/defaulted. */
		int sig=a1&037, h=a2&0xffff, old, hs;
		if(sig<1 || sig>=32){ errno=EINVAL; r=-1; break; }
		old = guest_sigh[sig]; guest_sigh[sig]=h;
		/* Mirror onto the host (translating the number) EXCEPT 2.8's
		 * synchronous fault signals 4-12 (ILL/TRAP/IOT/EMT/FPE/KILL/BUS/SEGV/
		 * SYS) and STOP: those are raised INTERNALLY by the emulator on a bad
		 * instruction / odd address, not caught from the host -- and hijacking
		 * the host SIGSEGV/SIGBUS would mask a real apsim crash.  The handler is
		 * still recorded so raise_fault() can find it. */
		hs = sig_g2h(sig);
		if((sig>=4 && sig<=12) || sig==SIGSTOP || hs==0) ;  /* fault/STOP: record only */
		else if(h==1) signal(hs, SIG_IGN);
		else if(h==0) signal(hs, SIG_DFL);
		else signal(hs, hsig);
		r = old; break;
	}
	case 37: {			/* kill(pid, sig): pid in r0 (V7 style) or on
					 * the stack (2.10/2.11); negative pid = pgrp. */
		int rawpid = stackargs ? (int)(short)a1 : (int)(short)R[0];
		int gsig = (stackargs ? a2 : a1)&037;
		int gp=guest_pid(), want=rawpid&0x7fff;
		if(rawpid>0 && want==gp){	/* kill self == raise: deliver via our
					 * own dispatch, NOT host kill -- a host fault
					 * signal (e.g. SIGABRT) would kill apsim. */
			if(gsig<1||gsig>=32){ errno=EINVAL; r=-1; }
			else if(guest_sigh[gsig]==1){ r=0; }		/* SIG_IGN: drop */
			else if(guest_sigh[gsig]>1){ pending_sig=gsig; r=0; }	/* handler */
			else switch(sig_dfl_action(gsig)){
			case 2: raise(SIGSTOP); r=0; break;
			case 1: r=0; break;
			default: halted=1; ecode=128+gsig; r=0; break;
			}
		} else {		/* another process or pgrp: host kill */
			int hs = sig_g2h(gsig);
			int hp = rawpid<0 ? -pid_g2h(-rawpid) : pid_g2h(rawpid);
			r = hs ? kill(hp, hs) : 0;
		}
		break;
	}
	case 24:			/* getuid -> ruid in r0, euid in r1 (geteuid reads r1) */
		R[1]=g_euid; r = g_ruid; break;
	case 47:			/* getgid -> rgid in r0, egid in r1 (getegid reads r1) */
		R[1]=g_egid; r = g_rgid; break;
	case 23:			/* setuid(uid in r0): sandbox allows it; set r&e uid */
		g_ruid = g_euid = R[0]&0xffff; r = 0; break;
	case 46:			/* setgid(gid in r0): set r&e gid */
		g_rgid = g_egid = R[0]&0xffff; r = 0; break;
	case 20:			/* getpid -> pid in r0, ppid in r1 (V7).  Real host
					 * pids so fork/wait have consistent identities. */
		R[1]=getppid()&0x7fff; r = guest_pid(); break;
	case 13:			/* time(a1=*tloc) -> seconds (low word r0, high r1).
					 * Deterministic: 2 by default, or $APSIM_TIME
					 * (decimal epoch seconds) for date-bearing
					 * output (nroff "Printed" footers etc). */
		{ long t=2; char *e=getenv("APSIM_TIME"); if(e) t=atol(e);
		  if(a1){ st2(a1,(int)(t>>16)); st2(a1+2,(int)(t&0xffff)); } R[1]=t&0xffff; r=(t>>16)&0xffff; }
		break;
	case 17:			/* break(a1=new break addr).  apsim's 64KB is
					 * flat-mapped, so the heap memory already
					 * exists; just succeed unless the new break
					 * would run into the stack (R6). */
		if((a1&0xffff) < (R[6]&0xffff)){ gbrk=a1&0xffff; r=0; }
		else { errno=ENOMEM; r=-1; }
		break;
	case 27:			/* alarm(sec in r0): real host alarm -> SIGALRM,
					 * forwarded to the guest's handler if installed. */
		r = alarm(R[0]&0xffff);
		break;
	/* ---- Tier 2: cheap syscalls (act where the host can, else succeed) ---- */
	case 9:				/* link(name1=a1, name2=a2) */
		{ char p1[1024]; strncpy(p1,mappath(a1),sizeof p1-1); p1[sizeof p1-1]=0;
		  r = link(p1, mappath(a2)); }
		break;
	case 16:			/* chown(path, uid[, gid]): 2 args through V6,
					 * 3 from V7.  Try; succeed even if the sandbox
					 * can't actually chown. */
		if(Kern <= PDP11_K_V56){ if(chown(mappath(a1), a2&0xff, -1)){} }
		else if(chown(mappath(a1), a2, stackargs?a3:ld2(argaddr+4))){}
		r = 0;
		break;
	case 14:			/* mknod(path, mode, dev): unprivileged -> EPERM. */
		errno = EPERM; r = -1;
		break;
	case 30:			/* V5: smdate (set mod date; accept, no effect);
					 * V6: inoperative; V7+: utime(path, timep[2]). */
		if(Kern <= PDP11_K_V56){ r = 0; break; }
		{ struct utimbuf u; int tp=a2&0xffff;
		  u.actime = ((long)(ld2(tp)&0xffff)<<16)|(ld2(tp+2)&0xffff);
		  u.modtime= ((long)(ld2(tp+4)&0xffff)<<16)|(ld2(tp+6)&0xffff);
		  r = utime(mappath(a1), &u); }
		break;
	case 35:			/* V5/V6: sleep(sec in r0): really sleep.
					 * V7+: ftime(&timeb) -- time + millitm + tz. */
		if(Kern <= PDP11_K_V56){ r = sleep(R[0]); break; }
		{ long t=2; char *e=getenv("APSIM_TIME"); if(e) t=atol(e);
		  stl(a1&0xffff, t); st2((a1+4)&0xffff, 0);	/* millitm */
		  st2((a1+6)&0xffff, 0); st2((a1+8)&0xffff, 0);	/* tz, dst */
		  r = 0; }
		break;
	case 29:			/* pause: block until a signal arrives, then the main
					 * loop delivers it; pause returns EINTR. */
		while(!pending_sig) pause();
		r = -1; break;
	case 43:			/* times(buf): zero CPU usage (no scheduler). */
		{ int b=a1&0xffff,k; for(k=0;k<8;k++) st2(b+2*k,0); r = 0; }
		break;
	case 34:			/* nice(incr): no scheduler -> succeed. */
	case 36:			/* sync: host fs already coherent. */
	case 39:			/* setpgrp: no job control -> succeed. */
	case 25:			/* stime: don't move the clock -> succeed. */
	case 44:			/* profil: accept and ignore the buckets. */
	case 51:			/* acct: process accounting -> succeed, no effect. */
	case 53:			/* lock: process-in-core -> succeed, no effect. */
		r = 0; break;
	case 61:			/* chroot: sandbox already roots paths via mappath;
					 * accept without changing the host root. */
		r = 0; break;
	case 21:			/* mount */
	case 22:			/* umount */
	case 26:			/* ptrace: no in-sim debugging */
	case 52:			/* phys: map physical memory (privileged) */
		r = -1; break;			/* EPERM-ish: unsupported, fail cleanly */
	case 32:			/* gtty(fd=r0, &sgttyb=a1): old-style tty get,
					 * = ioctl TIOCGETP. */
		if(!isatty(fd0)){ errno=ENOTTY; r=-1; break; }
		sgtty_get(a1&0xffff); r=0;
		break;
	case 31:			/* stty(fd=r0, &sgttyb=a1): old-style tty set,
					 * = ioctl TIOCSETP. */
		if(!isatty(fd0)){ errno=ENOTTY; r=-1; break; }
		tty_apply(ld2((a1&0xffff)+4)&0xffff); r=0;
		break;
	case 41:			/* dup(fd=r0) / dup2 (fd|0100 in r0, newfd a1) */
		if(fd0&0100) r = dup2(fd0&~0100, a1);
		else r = dup(fd0);
		break;
	case 12:			/* chdir(a1=path) */
		r = chdir(mappath(a1));
		break;
	case 59: {			/* exece(path, argv, envp): exec with an explicit
					 * environment.  Like exec(11) but the new env comes
					 * from the guest envp, copied to host strings first. */
		static char ab[64][256], eb[64][256]; char *av[64], *ev[64]; char hp[1024];
		int na=0, ne=0, ap=a2&0xffff, ep=ld2(argaddr+4)&0xffff, p;
		strncpy(hp, mappath(a1), sizeof hp-1); hp[sizeof hp-1]=0;
		while(na<63){ p=ld2(ap); ap+=2; if(!p)break; strncpy(ab[na],(char*)(M+(p&0xffff)),255); ab[na][255]=0; av[na]=ab[na]; na++; }
		while(ep && ne<63){ p=ld2(ep); ep+=2; if(!p)break; strncpy(eb[ne],(char*)(M+(p&0xffff)),255); eb[ne][255]=0; ev[ne]=eb[ne]; ne++; }
		if(load_aout_env(hp, na, av, ne, ev)<0){ FC=1; R[0]=ENOENT; break; }
		return;			/* success: run the new program */
	}
	case 58: {			/* local(sub): 2.8's site-syscall dispatcher.
					 * The sub-call's `sys' word follows in the block
					 * (a1 holds it).  rogue uses gldav (14) to read the
					 * load average -- report idle so it always runs. */
		int sub = a1 & 0377;
		if(sub==14){		/* gldav(av): short av[3] at R0, all zero */
			int av=R[0]&0xffff; st2(av,0); st2(av+2,0); st2(av+4,0); r=0;
		} else r=0;		/* other local calls: succeed, no effect */
		break;
	}
	/* ---- canonical extensions (no V7 ancestor; reached via remap) ---- */
	case C_SIGVEC: {		/* 4.3-style sigvec(108)/sigaction(31).  The
					 * libc stub prepends the sigtramp address
					 * as a hidden first arg (see sigaction.s),
					 * so the kernel-level args are
					 * (sigtramp, sig, vec, ovec).  vec is a
					 * {handler, long mask, flags}. */
		int tramp=a1&0xffff, sig=a2&037, vec=a3&0xffff;
		int ovec=(stackargs?ld2((SP+8)&0xffff):ld2(argaddr+6))&0xffff;
		int h, hs2; long hm;
		if(sig<1 || sig>=32){ errno=EINVAL; r=-1; break; }
		if(tramp) guest_sigtramp=tramp;
		if(ovec){
			st2(ovec, guest_sigh[sig]);
			stl(ovec+2, guest_hmask[sig]);
			st2(ovec+6, 0);
		}
		if(vec){
			h  = ld2(vec);
			hm = ((long)(ld2(vec+2)&0xffff)<<16)|(ld2(vec+4)&0xffff);
			guest_sigh[sig]=h;
			guest_hmask[sig]=hm;
			guest_reliable[sig]=1;
			hs2 = sig_g2h(sig);
			if((sig>=4 && sig<=12) || hs2==0) ;	/* fault sigs: record only */
			else if(h==1) signal(hs2, SIG_IGN);
			else if(h==0) signal(hs2, SIG_DFL);
			else signal(hs2, hsig);
		}
		r = 0; break;
	}
	case C_SIGRETURN: {		/* sigreturn(scp): restore the context the
					 * delivery frame saved -- sp, fp, r0, r1,
					 * pc, ps, and the blocked mask. */
		int scp=(stackargs?ld2((SP+2)&0xffff):ld2(argaddr))&0xffff;
		guest_sigmask = ((long)(ld2(scp+2)&0xffff)<<16)|(ld2(scp+4)&0xffff);
		R[6]=ld2(scp+6); R[5]=ld2(scp+8);
		R[1]=ld2(scp+10); R[0]=ld2(scp+12);
		PC=ld2(scp+14);
		{ int ps=ld2(scp+16); FC=ps&1; FV=(ps>>1)&1; FZ=(ps>>2)&1; FN=(ps>>3)&1; }
		return;
	}
	case C_WAIT4: {			/* wait4(pid, *status, options, *rusage):
					 * WNOHANG=1, WUNTRACED=2; a stopped child
					 * reports (stopsig<<8)|0177. */
		int st, opts=((a3&1)?WNOHANG:0)|((a3&2)?WUNTRACED:0);
		int who = (a1==0xffff||a1==0) ? -1 : pid_g2h((int)(short)a1<0?-(int)(short)a1:(int)(short)a1);
		if((int)(short)a1<0 && a1!=0xffff) who=-who;	/* negative = pgrp */
		int wpid = waitpid(who, &st, opts);
		if(wpid<0){ r=-1; break; }
		if(wpid==0){ if(a2) st2(a2&0xffff,0); r=0; break; }
		if(a2){
			int gst;
			if(WIFSTOPPED(st))     gst=((sig_h2g(WSTOPSIG(st))<<8)|0177);
			else if(WIFEXITED(st)) gst=(WEXITSTATUS(st)&0xff)<<8;
			else                   gst=sig_h2g(WTERMSIG(st))&0x7f;
			st2(a2&0xffff, gst&0xffff);
		}
		r = wpid&0x7fff; break;
	}
	case C_SBRK: {			/* 2.10/2.11 sys sbrk: the libc stub tracks
					 * curbrk itself and passes the ABSOLUTE new
					 * break ("nsiz", like break); the syscall's
					 * return value is discarded -- only the carry
					 * matters. */
		if((a1&0xffff) >= (SP&0xffff)){ errno=ENOMEM; r=-1; break; }
		gbrk=a1&0xffff; r=0; break;
	}
	case C_GETPAGESIZE: r = 1024; break;	/* 2.11 ctob(CLSIZE) */
	case C_GETDTABLESIZE: r = 30; break;	/* NOFILE */
	case C_DUP2:
		r = dup2(fd0, a1); break;
	case C_GETPPID: r = getppid()&0x7fff; break;
	case C_GETEUID: r = g_euid; break;
	case C_GETEGID: r = g_egid; break;
	case C_GETPGRP:			/* getpgrp(pid): the real host pgrp */
		r = getpgid(pid_g2h(a1)); if(r>0) r&=0x7fff; break;
	case C_SETPGRP:			/* setpgrp(pid, pgrp) -> host setpgid; this
					 * is what puts a csh job in its own process
					 * group so the HOST kernel routes tty stops
					 * and signals to it. */
		r = setpgid(pid_g2h(a1), pid_g2h(a2)); break;
	case C_SIGBLOCK: {		/* sigblock(long mask): OR into the mask.
					 * The mask is a 32-bit long (SIGCHLD is
					 * bit 19), passed high-word-first and
					 * returned in the r0:r1 pair. */
		long old2=guest_sigmask;
		guest_sigmask |= ((long)(a2&0xffff)<<16)|(a1&0xffff);
		R[1]=old2&0xffff; r=(old2>>16)&0xffff; break;
	}
	case C_SIGSETMASK: {		/* sigsetmask(long mask) */
		long old2=guest_sigmask;
		guest_sigmask = ((long)(a2&0xffff)<<16)|(a1&0xffff);
		R[1]=old2&0xffff; r=(old2>>16)&0xffff; break;
	}
	case C_SIGSUSPEND: {		/* sigsuspend(mask): adopt the mask, wait
					 * for a deliverable signal, return EINTR
					 * with the old mask back -- csh's whole
					 * job-wait loop turns on this. */
		long omask=guest_sigmask;
		long nmask=((long)(a2&0xffff)<<16)|(a1&0xffff);
		guest_sigmask=nmask;
		while(!(pending_sig && !((guest_sigmask>>(pending_sig-1))&1)))
			pause();
		guest_sigmask=omask;
		errno=EINTR; r=-1; break;
	}
	case C_GETGROUPS:		/* getgroups(n, *gids): one group */
		if(a1>=1 && a2) st2(a2&0xffff, g_rgid);
		r = 1; break;
	case C_GETLOGIN: {		/* getlogin() -> static name; libc copies it */
		/* 2.11 getlogin(2) writes into a buffer: args (buf, len) */
		int b=a1&0xffff, n=a2&0xffff;
		if(b && n>0){ const char *nm="root"; int k;
			for(k=0;k<n-1 && nm[k];k++) st1(b+k,nm[k]); st1(b+k,0); }
		r = 0; break;
	}
	case C_GETTIMEOFDAY: {		/* gettimeofday(*tv, *tz) */
		long t=2; char *e=getenv("APSIM_TIME"); if(e) t=atol(e);
		if(a1){ stl(a1&0xffff,t); stl((a1+4)&0xffff,0); }
		if(a2){ stl(a2&0xffff,0); }
		r = 0; break;
	}
	case C_GETRUSAGE: {		/* getrusage(who, *ru): zeroed usage */
		int b=a2&0xffff, k;
		if(b) for(k=0;k<36;k+=2) st2(b+k,0);
		r = 0; break;
	}
	case C_GETRLIMIT: {		/* getrlimit(res, *rlp): "unlimited" */
		int b=a2&0xffff;
		if(b){ stl(b,0x7fffffffL); stl(b+4,0x7fffffffL); }
		r = 0; break;
	}
	case C_FCNTL:			/* fcntl(fd, cmd, arg): F_DUPFD works; the
					 * flag get/set commands report benign values */
		if(a1==0){ r = fcntl(fd0, F_DUPFD, a2); }
		else r = 0;
		break;
	case C_SELECT: {		/* select(nfds, *r, *w, *e, *tv): guest fds are
					 * host fds, so translate the 16-bit masks. */
		fd_set rs, ws; struct timeval tv, *tvp=0;
		int nf=a1&0xffff, rm=a2?ld2(a2&0xffff):0, wm=a3?ld2(a3&0xffff):0, k;
		int ea=stackargs?ld2((SP+8)&0xffff):ld2(argaddr+6);
		int ta=stackargs?ld2((SP+10)&0xffff):ld2(argaddr+8);
		if(nf>16) nf=16;
		FD_ZERO(&rs); FD_ZERO(&ws);
		for(k=0;k<nf;k++){ if(rm&(1<<k)) FD_SET(k,&rs); if(wm&(1<<k)) FD_SET(k,&ws); }
		if(ta){ long sec=((long)(ld2(ta&0xffff)&0xffff)<<16)|(ld2((ta+2)&0xffff)&0xffff);
			long usec=((long)(ld2((ta+4)&0xffff)&0xffff)<<16)|(ld2((ta+6)&0xffff)&0xffff);
			tv.tv_sec=sec; tv.tv_usec=usec; tvp=&tv; }
		r = select(nf, &rs, &ws, 0, tvp);
		if(r>=0){
			int nrm=0, nwm=0;
			for(k=0;k<nf;k++){ if(FD_ISSET(k,&rs)) nrm|=1<<k; if(FD_ISSET(k,&ws)) nwm|=1<<k; }
			if(a2) st2(a2&0xffff,nrm);
			if(a3) st2(a3&0xffff,nwm);
			if(ea) st2(ea&0xffff,0);
		}
		break;
	}
	case C_READV: case C_WRITEV: {	/* readv/writev(fd, *iov, cnt) */
		int iov=a1&0xffff, cnt=a2&0xffff, k; long tot=0;
		if(cnt>16) cnt=16;
		r=0;
		for(k=0;k<cnt;k++){
			int base=ld2(iov+4*k)&0xffff, len=ld2(iov+4*k+2)&0xffff;
			long n = (num==C_READV) ? read(fd0, M+base, len)
			                        : write(fd0, M+base, len);
			if(n<0){ r=-1; break; }
			tot+=n;
			if(n<len) break;
		}
		if(r>=0) r=tot;
		break;
	}
	case C_MKDIR:  r = mkdir(mappath(a1), a2&07777); break;
	case C_RMDIR:  r = rmdir(mappath(a1)); break;
	case C_RENAME: {
		char p1[1024]; strncpy(p1,mappath(a1),sizeof p1-1); p1[sizeof p1-1]=0;
		r = rename(p1, mappath(a2)); break;
	}
	case C_SYMLINK: {
		char p1[1024]; strncpy(p1,(char*)(M+(a1&0xffff)),sizeof p1-1); p1[sizeof p1-1]=0;
		r = symlink(p1, mappath(a2)); break;	/* target string verbatim */
	}
	case C_READLINK: {
		char lb[1024]; int n2, b=a2&0xffff, max=a3&0xffff, k;
		n2 = readlink(mappath(a1), lb, sizeof lb-1);
		if(n2<0){ r=-1; break; }
		if(n2>max) n2=max;
		for(k=0;k<n2;k++) st1(b+k, lb[k]);
		r = n2; break;
	}
	case C_TRUNCATE: {
		long len=((long)(a2&0xffff)<<16)|(a3&0xffff);
		r = truncate(mappath(a1), len); break;
	}
	case C_FTRUNCATE: {
		long len=((long)(a1&0xffff)<<16)|(a2&0xffff);
		r = ftruncate(fd0, len); break;
	}
	case C_FCHMOD:  r = fchmod(fd0, a1&07777); break;
	case C_FSYNC:   r = fsync(fd0); break;
	case C_FCHDIR:  r = fchdir(fd0); break;
	case C_UTIMES: {		/* utimes(path, tv[2]) -> utime */
		struct utimbuf u; int tp=a2&0xffff;
		u.actime = ((long)(ld2(tp)&0xffff)<<16)|(ld2(tp+2)&0xffff);
		u.modtime= ((long)(ld2(tp+8)&0xffff)<<16)|(ld2(tp+10)&0xffff);
		r = utime(mappath(a1), &u); break;
	}
	case C_GETHOSTNAME: {		/* gethostname(buf, len) */
		int b=a1&0xffff, n=a2&0xffff; const char *nm="apsim"; int k;
		if(b && n>0){ for(k=0;k<n-1 && nm[k];k++) st1(b+k,nm[k]); st1(b+k,0); }
		r = 0; break;
	}
	case C_KILLPG: {		/* killpg(pgrp, sig): real host process group */
		int hs=sig_g2h(a2&037);
		r = hs ? killpg(pid_g2h(a1), hs) : 0; break;
	}
	case C_UNAME: {			/* sys3/Ultrix utssys(buf, 0, 0): 5 x 9-char
					 * fields sysname/node/release/version/machine */
		int b=a1&0xffff, k; const char *f[5]={"apsim","apsim","3","1","pdp11"};
		for(k=0;k<5;k++){ int j; for(j=0;j<9;j++) st1(b+9*k+j, j<(int)strlen(f[k])?f[k][j]:0); }
		r = 0; break;
	}
	case C_ULIMIT:			/* ulimit(cmd, val): report a big limit */
		r = 0x7fff; break;
	case C_NAP:			/* Ultrix nap(ms): sleep in ticks */
		{ struct timespec ts; long ms=(long)(a1&0xffff);
		  ts.tv_sec=ms/1000; ts.tv_nsec=(ms%1000)*1000000L;
		  nanosleep(&ts,0); r=0; }
		break;
	case C_SYSCTL: {		/* 2.11 pl431 __sysctl(name, namelen, old,
					 * *oldlenp, new, newlen): late 2.11 libc reaches
					 * hostname/uname facts through this.  Answer the
					 * common CTL_KERN/CTL_HW string and int nodes. */
		int name=a1&0xffff, oldp=a3&0xffff;
		int oldlenp=ld2((SP+10)&0xffff)&0xffff;	/* 4th stack arg */
		int top=ld2(name)&0xffff, sub=ld2(name+2)&0xffff;
		const char *s=0; long iv=-1;
		if(top==1){		/* CTL_KERN */
			if(sub==1) s="2.11BSD";		/* KERN_OSTYPE */
			else if(sub==2) s="2.11BSD";	/* KERN_OSRELEASE */
			else if(sub==4) s="2.11 BSD UNIX (apsim)";	/* KERN_VERSION */
			else if(sub==10) s="apsim";	/* KERN_HOSTNAME */
		} else if(top==6){	/* CTL_HW */
			if(sub==7) iv=1024;		/* HW_PAGESIZE */
			else if(sub==3) iv=1;		/* HW_NCPU */
		}
		if(s){
			int k, n2=(int)strlen(s)+1;
			if(oldp) for(k=0;k<n2;k++) st1(oldp+k, s[k]);
			if(oldlenp) st2(oldlenp, n2);
			r=0;
		} else if(iv>=0){
			if(oldp) st2(oldp, (int)iv);
			if(oldlenp) st2(oldlenp, 2);
			r=0;
		} else { errno=ENOSYS; r=-1; }
		break;
	}
	case C_OK:			/* benign per-era no-ops (setpgrp, itimers,
					 * sigblock/sigsetmask, flock, hostid, ...) */
		r = 0; break;
	case C_NOSYS:
		errno = ENOSYS; r = -1; break;
	default:
		fprintf(stderr, "apsim: unhandled sys %d (universe %s) -> ENOSYS\n", num, Univ);
		errno = ENOSYS; r = -1; break;
	}
	if(systrace) fprintf(stderr, "    -> %ld\n", r);
	if(r<0 && errno==EINTR && (num==3||num==4)){
		PC=TrapPC;	/* slow read/write: deliver the signal, then
				 * restart (the kernel's ERESTART).  wait is
				 * NOT restarted -- a SIGCHLD handler that
				 * reaps the child would leave a restarted
				 * wait blocking forever (csh's job loop). */
		return;
	}
	if(r<0){ FC=1; R[0]=errno_h2g(errno); }	/* error: carry + era errno in r0 */
	else { FC=0; R[0]=r&0xffff; }
}

/* a `sys' (trap) instruction: 0104400|n.  n==0 is the indirect call, whose
 * following word addresses a block { sysinstr; arg1; arg2; ... }. */
/* inline-argument word count for the DIRECT `sys N; arg; arg' form (raw-asm
 * callers like `as').  fd-style first args ride in R0, so they are NOT counted.
 * The kernel skips these words past the trap; apsim must too, or the args get
 * executed as instructions.  libc uses the indirect `sys 0;block' form, whose
 * args live in the block (not the code stream), so this doesn't apply there. */
static int sysnargs(int num){
	/* inline words = (total arg words) - (args in registers), per syscall,
	 * taken verbatim from Apout's v7arg[] (v7trap.c).  fd-style first args
	 * ride in R0 and are not inline. */
	static const signed char inl[64]={
	/*0*/	0,0,0,2,2,2,0,0,  /* indir exit fork read write open close wait */
	/*8*/	2,2,1,2,1,0,3,2,  /* creat link unlink exec chdir time mknod chmod */
	/*16*/	3,1,2,3,0,3,1,0,  /* chown break stat lseek getpid mount umount setuid */
	/*24*/	0,0,3,0,1,0,2,1,  /* getuid stime ptrace alarm fstat pause utime stty */
	/*32*/	1,2,0,1,0,1,0,0,  /* gtty access nice sleep sync kill csw setpgrp */
	/*40*/	0,0,0,1,4,0,0,0,  /* - dup pipe times profil - setgid getgid */
	/*48*/	2,0,0,1,3,1,3,0,  /* signal - - acct phys lock ioctl reboot */
	/*56*/	4,0,0,3,1,1,0,0 };/* mpx - - exece umask chroot - - */
	if(Kern <= PDP11_K_V56){
		/* the V5/V6 sysent counts where they differ from V7's:
		 * chown 2 args, seek 2 (16-bit offset), smdate 1, sleep 0 */
		switch(num){
		case 16: return 2;
		case 19: return 2;
		case 30: return 1;
		case 35: return 0;
		}
	}
	return (num>=0 && num<64) ? inl[num] : 2;
}
/* ---- First Edition trap layer -----------------------------------------
 * The 1971-72 convention: no indirect call (trap 0 is `rele'), per-call
 * inline argument words after the trap (nwords - nregs, from the V1
 * kernel's sysent dispatch as transcribed in apout's v1trap.c), fd-style
 * first args in r0, and the C bit for errors.  Results that are values
 * return in r0; `time' returns in the KE11-A's AC/MQ pair. */
static const signed char v1inl[35] = {
/*0*/	0,0,0,2,2,2,0,0,	/* rele exit fork read write open close wait */
/*8*/	2,2,1,2,1,0,2,2,	/* creat link unlink exec chdir time makdir chmod */
/*16*/	2,1,2,2,2,2,1,0,	/* chown break stat seek tell mount umount setuid */
/*24*/	0,0,1,1,1,1,1,1,	/* getuid stime quit intr fstat cemt mdate stty */
/*32*/	1,1,0 };		/* gtty ilgins nice */

/* V1 mode bits (i-node flags), for chmod/creat/makdir and the stat report */
#define V1M_SUID 040
#define V1M_EXEC 020
#define V1M_OR   010
#define V1M_OW   04
#define V1M_WR   02
#define V1M_WW   01
static int v1mode_to_host(int m){
	int h=0;
	if(m&V1M_SUID) h|=04000;
	if(m&V1M_EXEC) h|=0111;
	if(m&V1M_OR)   h|=0400;
	if(m&V1M_OW)   h|=0200;
	if(m&V1M_WR)   h|=044;
	if(m&V1M_WW)   h|=022;
	return h;
}
/* 60ths of a second since the start of the current year -- the V1 clock
 * (apout's kludge: the real epoch, 1971-01-01, overflows 32 bits by now) */
static long v1sixty(time_t t){
	struct tm tm = *localtime(&t);
	tm.tm_mon=0; tm.tm_mday=1; tm.tm_hour=0; tm.tm_min=0; tm.tm_sec=0;
	return (long)(t - mktime(&tm)) * 60;
}
/* the 34-byte V1 stat structure: inum, flags, nlinks, uid, size,
 * iaddr[8] (zeroed), ctime, mtime (60ths, high word first), unused */
static void v1statout(struct stat *hs, int sb){
	int fl = 0100000|020000;			/* used|modified */
	long c=v1sixty(hs->st_ctime), m=v1sixty(hs->st_mtime);
	int ino = hs->st_ino&0x7fff; if(ino<41) ino+=100;  /* <41 = device inodes */
	if(hs->st_size>4095)        fl|=010000;		/* large file */
	if(S_ISDIR(hs->st_mode))    fl|=040000;
	if(hs->st_mode&04000)       fl|=V1M_SUID;
	if(hs->st_mode&0100)        fl|=V1M_EXEC;
	if(hs->st_mode&0400)        fl|=V1M_OR;
	if(hs->st_mode&0200)        fl|=V1M_OW;
	if(hs->st_mode&04)          fl|=V1M_WR;
	if(hs->st_mode&02)          fl|=V1M_WW;
	st2(sb+0,ino); st2(sb+2,fl);
	st1(sb+4,hs->st_nlink&0xff); st1(sb+5,hs->st_uid&0xff);
	st2(sb+6,hs->st_size&0xffff);
	{ int k; for(k=0;k<8;k++) st2(sb+8+2*k,0); }	/* iaddr[8] */
	st2(sb+24,(c>>16)&0xffff); st2(sb+26,c&0xffff);
	st2(sb+28,(m>>16)&0xffff); st2(sb+30,m&0xffff);
	st2(sb+32,0);
}
static void do_v1syscall(int num, int argaddr)
{
	int a1=ld2(argaddr), a2=ld2(argaddr+2);
	long r=0;
	if(systrace) fprintf(stderr,"v1sys %d r0=%06o a1=%06o a2=%06o\n",num,R[0],a1,a2);
	switch(num){
	case 0:					/* rele: give up the processor */
	case 17:				/* break: grow the data area (flat here) */
	case 26: case 27:			/* quit / intr: trap-catch switches */
	case 29: case 33:			/* cemt / ilgins */
	case 34:				/* nice */
		FC=0; return;
	case 25: case 31: case 32:		/* stime / stty / gtty: succeed */
		FC=0; return;
	case 30:				/* mdate: set mod date (from AC/MQ) */
		FC=0; return;
	case 21: case 22:			/* mount / umount */
		FC=1; return;
	case 1:					/* exit: V1 took NO status -- r0 is junk
						 * (even the s2 tape's as doesn't set it),
						 * so report 0.  $APSIM_V1EXIT=r0 opts in
						 * to the s2 convention for 1972 binaries
						 * that do set r0. */
		halted=1;
		{ char *e=getenv("APSIM_V1EXIT");
		  ecode = (e && !strcmp(e,"r0")) ? (R[0]&0xff) : 0; }
		return;
	case 2: {				/* fork: parent skips the child branch */
		int pid=fork();
		if(pid<0){ FC=1; return; }
		if(pid>0){ FC=0; R[0]=pid&0x7fff; PC=(PC+2)&0xffff; return; }
		FC=0; R[0]=getppid()&0x7fff; return;	/* child: r0 = parent pid */
	}
	case 7: {				/* wait: r0 = pid, MQ = status (s2) */
		int st, wpid=wait(&st);
		if(wpid<0){ FC=1; KMQ=0; return; }
		R[0]=wpid&0x7fff;
		KMQ=(WIFEXITED(st)?WEXITSTATUS(st):0)&0xff;
		FC=0; return;
	}
	case 3:					/* read(fd=r0; buf, count) */
		r=read(R[0],(char*)M+(a1&0xffff),a2&0xffff);
		if(r<0){ FC=1; return; }
		R[0]=r&0xffff; FC=0; return;
	case 4:					/* write(fd=r0; buf, count) */
		r=write(R[0],(char*)M+(a1&0xffff),a2&0xffff);
		if(r<0){ FC=1; return; }
		R[0]=r&0xffff; FC=0; return;
	case 5: {				/* open(name, mode 0/1/2) */
		int hm = a2==0?O_RDONLY : a2==1?O_WRONLY : O_RDWR;
		r=open(mappath(a1),hm);
		if(r<0){ FC=1; return; }
		R[0]=r&0xffff; FC=0; return;
	}
	case 6:					/* close(fd=r0) */
		FC = close(R[0])<0; return;
	case 8:					/* creat(name, mode) */
		r=creat(mappath(a1), v1mode_to_host(a2));
		if(r<0){ FC=1; return; }
		R[0]=r&0xffff; FC=0; return;
	case 9: {				/* link(old, new) */
		char oldp[1024];
		strncpy(oldp,mappath(a1),sizeof oldp -1); oldp[sizeof oldp -1]=0;
		FC = link(oldp,mappath(a2))<0; return;
	}
	case 10:				/* unlink(name) */
		FC = unlink(mappath(a1))<0; return;
	case 11: {				/* exec(name, argp): argp -> ptrs, 0-end */
		static char argbuf[32][256]; char *args[32]; char hp[1024];
		int n=0, aptr=a2&0xffff, p;
		strncpy(hp,mappath(a1),sizeof hp -1); hp[sizeof hp-1]=0;
		while(n<32){ p=ld2(aptr); aptr+=2; if(p==0||p==0177777) break;
			strncpy(argbuf[n],(char*)(M+(p&0xffff)),255); argbuf[n][255]=0;
			args[n]=argbuf[n]; n++; }
		if(load_aout(hp,n,args)<0){ FC=1; return; }
		return;				/* running the new image */
	}
	case 12:				/* chdir(name) */
		FC = chdir(mappath(a1))<0; return;
	case 13: {				/* time -> AC:MQ, 60ths since year start */
		long t=v1sixty(time(0));
		KAC=(t>>16)&0xffff; KMQ=t&0xffff; FC=0; return;
	}
	case 14:				/* makdir(name, mode) */
		FC = mkdir(mappath(a1), v1mode_to_host(a2)|0700)<0; return;
	case 15:				/* chmod(name, mode) */
		FC = chmod(mappath(a1), v1mode_to_host(a2))<0; return;
	case 16:				/* chown(name, owner) */
		FC = chown(mappath(a1), a2&0x3fff, -1)<0; return;
	case 18: {				/* stat(name; buf) */
		struct stat hs;
		if(stat(mappath(a1),&hs)<0){ FC=1; return; }
		v1statout(&hs,a2&0xffff); FC=0; return;
	}
	case 28: {				/* fstat(fd=r0; buf) */
		struct stat hs;
		if(fstat(R[0],&hs)<0){ FC=1; return; }
		v1statout(&hs,a1&0xffff); FC=0; return;
	}
	case 19: {				/* seek(fd=r0; offset, ptrname) */
		long off = (a2==0) ? (long)(a1&0xffff) : (long)(short)a1;
		r=lseek(R[0],off,a2);
		if(r<0){ FC=1; return; }
		R[0]=0; FC=0; return;
	}
	case 20:				/* tell(fd=r0; ...) -> offset in r0 */
		r=lseek(R[0],0,SEEK_CUR);
		if(r<0){ FC=1; return; }
		R[0]=r&0xffff; FC=0; return;
	case 23:				/* setuid(uid=r0) */
		g_ruid=g_euid=R[0]&0xff; FC=0; return;
	case 24:				/* getuid -> r0 */
		R[0]=g_ruid&0xffff; FC=0; return;
	default:
		fprintf(stderr,"apsim: V1 sys %d not implemented (pc=%06o)\n",num,(PC-2)&0xffff);
		halted=1; ecode=127; return;
	}
}
static void do_v1sys(int instr)
{
	int num=instr&077, argaddr=PC;
	if(num>34){ fprintf(stderr,"apsim: bad V1 sys %o at pc=%06o\n",num,(PC-2)&0xffff);
		halted=1; ecode=127; return; }
	PC=(PC+2*v1inl[num])&0xffff;	/* step past the inline args (exec resets PC) */
	do_v1syscall(num, argaddr);
}

static void do_sys(int instr)
{
	int n=instr&0377, argaddr, num;
	TrapPC=(PC-2)&0xffff;
	if(v1sys){ do_v1sys(instr); return; }
	if(n==0){			/* indirect */
		int blk=ifetch(PC); PC=(PC+2)&0xffff;
		num=ld2(blk)&0377;	/* the real sys instruction's number */
		argaddr=blk+2;
	} else {			/* direct: inline args follow the trap.  Under the
					 * stack-args convention (2.10/2.11) a bare `sys N'
					 * has NO inline words -- skipping would eat real
					 * instructions after the trap. */
		num=n; argaddr=PC;
		if(!stackargs)
			PC=(PC+2*sysnargs(num))&0xffff;	/* skip them (exec resets PC itself) */
	}
	do_syscall(num, argaddr);
}

static int cond(int instr){		/* branch taken? */
	switch(instr&0177400){
	case 0000400: return 1;				/* BR   */
	case 0001000: return !FZ;			/* BNE  */
	case 0001400: return FZ;			/* BEQ  */
	case 0002000: return (FN^FV)==0;		/* BGE  */
	case 0002400: return (FN^FV)!=0;		/* BLT  */
	case 0003000: return (FZ|(FN^FV))==0;		/* BGT  */
	case 0003400: return (FZ|(FN^FV))!=0;		/* BLE  */
	case 0100000: return !FN;			/* BPL  */
	case 0100400: return FN;			/* BMI  */
	case 0101000: return !(FC|FZ);			/* BHI  */
	case 0101400: return FC|FZ;			/* BLOS */
	case 0102000: return !FV;			/* BVC  */
	case 0102400: return FV;			/* BVS  */
	case 0103000: return !FC;			/* BCC/BHIS */
	case 0103400: return FC;			/* BCS/BLO  */
	}
	return -1;				/* not a branch */
}

/* FP11 floating-point instruction (017xxxx).  AC[ac] is the accumulator in
 * bits 6-7; bits 0-5 are a source/dest operand (a float accumulator when the
 * mode is register-direct, else a float/int in memory).  Memory floats use
 * the current FPS precision, except the convert forms (movof/movfo) which use
 * the OTHER precision.  c1 only ever uses ac0/ac1 and non-autoincrement modes. */
/* Effective address of a float memory operand.  The autoincrement/decrement
 * modes (Rn)+ and -(Rn) must step by the FLOAT size (8 for D, 4 for F), not
 * the 2 that operand() assumes -- e.g. `movf r0,-(sp)' pushes a whole double.
 * The deferred forms @(Rn)+/@-(Rn) step a word (the pointer) and the index
 * modes are unaffected, so those delegate to operand(). */
static int fp_addr(int spec, int dbl){
	int mode=(spec>>3)&7, rn=spec&7, size=dbl?8:4, a;
	if(mode==2){ a=R[rn]; R[rn]=(R[rn]+size)&0xffff; return a; }	/* (Rn)+ */
	if(mode==4){ R[rn]=(R[rn]-size)&0xffff; return R[rn]; }		/* -(Rn) */
	return operand(spec,0);
}
static struct fpv fp_get(int spec, int dbl){	/* float source operand */
	if(((spec>>3)&7)==0) return AC[spec&7];
	if(((spec>>3)&7)==2 && (spec&7)==7){	/* #imm: a 1-word F-high literal
						 * (e.g. modf $one) -- the assembler
						 * emits only the high word. */
		int hi=ifetch(PC); PC=(PC+2)&0xffff;
		return bits2fpv((unsigned long long)(unsigned short)hi << 48);
	}
	return rdfloat(fp_addr(spec,dbl), dbl);
}
static void fp_put(int spec, struct fpv v, int dbl){	/* float dest operand */
	if(((spec>>3)&7)==0){ AC[spec&7]=v; return; }
	wrfloat(fp_addr(spec,dbl), v, dbl);
}
/* Effective address of an INTEGER memory operand of the convert forms
 * (movif/movfi -- LDCIF/LDCLF/STCFI/STCFL): autoincrement/decrement steps
 * by the integer size selected by FPS.FL -- 4 in Long mode, 2 in Integer
 * mode.  Resolving these through the word-size operand() made
 * `stcfl AC,-(sp)' push 4 bytes but move SP by 2: 2.11's FPU ldiv then
 * pops its own return address and `rts' lands at address 0, restarting
 * crt0 -- the bug behind the 2.11 ls/date meltdowns. */
static int fpi_addr(int spec){
	int mode=(spec>>3)&7, rn=spec&7, size=(FPS&FPL)?4:2, a;
	if(mode==2 && rn!=7){ a=R[rn]; R[rn]=(R[rn]+size)&0xffff; return a; }
	if(mode==4){ R[rn]=(R[rn]-size)&0xffff; return R[rn]; }
	return operand(spec,0);
}
static void fp_setcc(struct fpv v){ fN=(v.frac!=0)&&v.sign; fZ=(v.frac==0); fV=0; fC=0; }

static void do_fp(int instr){
	int op=instr&0177400, ac=(instr>>6)&3, spec=instr&077;
	int isreg=((spec>>3)&7)==0, reg=spec&7, dbl=(FPS&FPD)?1:0;
	struct fpv sv; int addr; long iv;

	switch(instr){				/* no-operand / mode control */
	case 0170000: FN=fN; FZ=fZ; FV=fV; FC=fC; return;	/* cfcc */
	case 0170001: FPS&=~FPD; return;			/* setf */
	case 0170011: FPS|= FPD; return;			/* setd */
	case 0170002: FPS&=~FPL; return;			/* seti */
	case 0170012: FPS|= FPL; return;			/* setl */
	}
	/* single-operand ops + ldfps/stfps: the opcode is bits 15-6 (no AC
	 * field), so they must be matched with the 0177700 mask -- clrf/tstf/
	 * absf/negf differ only in bits 7-6, which 0177400 would drop. */
	switch(instr&0177700){
	case 0170100: FPS=isreg?R[reg]:ld2(operand(spec,0)); return;	/* ldfps */
	case 0170300:	/* stst: FEC -> dst (+FEA if memory).  The lenient
			 * engine (div0 -> 0, no traps) never latches an
			 * error, so both read as zero. */
		if(isreg) R[reg]=0;
		else { addr=operand(spec,0); st2(addr,0); st2((addr+2)&0xffff,0); }
		return;
	case 0170200: if(isreg)R[reg]=FPS; else st2(operand(spec,0),FPS); return; /* stfps */
	case 0170400:	/* clrf */
		fp_put(spec,fp_zero(),dbl); fN=0; fZ=1; fV=0; fC=0; return;
	case 0170500:	/* tstf */
		fp_setcc(fp_get(spec,dbl)); return;
	case 0170600:	/* absf */
		sv=fp_get(spec,dbl); sv.sign=0; fp_put(spec,sv,dbl); fp_setcc(sv); return;
	case 0170700:	/* negf */
		sv=fp_negv(fp_get(spec,dbl)); fp_put(spec,sv,dbl); fp_setcc(sv); return;
	}
	/* two-operand ops: opcode bits 15-8, AC in bits 7-6 */
	switch(op){
	case 0171400:	/* modf: product = AC[ac]*fsrc; AC[ac|1]=int, AC[ac]=frac */
		{ struct fpv p=fp_mulv(AC[ac],fp_get(spec,dbl)), ip, fr;
		  if(p.frac==0 || p.exp<=0){ ip=fp_zero(); fr=p; }
		  else if(p.exp>=56){ ip=p; fr=fp_zero(); }
		  else{
			unsigned long long mask=(1ULL<<(56-p.exp))-1;
			unsigned long long ifr=p.frac&~mask, ffr=p.frac&mask;
			ip = ifr ? fp_norm128(p.sign,p.exp,(u128fp)ifr<<64,56) : fp_zero();
			fr = ffr ? fp_norm128(p.sign,p.exp,(u128fp)ffr<<64,56) : fp_zero();
		  }
		  AC[ac|1]=ip; AC[ac]=fr; fp_setcc(AC[ac]); }
		return;
	case 0171000: AC[ac]=fp_mulv(AC[ac],fp_get(spec,dbl)); fp_setcc(AC[ac]); return;	/* mulf */
	case 0172000: AC[ac]=fp_addv(AC[ac],fp_get(spec,dbl)); fp_setcc(AC[ac]); return;	/* addf */
	case 0173000: AC[ac]=fp_addv(AC[ac],fp_negv(fp_get(spec,dbl))); fp_setcc(AC[ac]); return;	/* subf */
	case 0174400: AC[ac]=fp_divv(AC[ac],fp_get(spec,dbl)); fp_setcc(AC[ac]); return;	/* divf */
	case 0173400:	/* cmpf: CC from (fsrc-AC), compared exactly */
		{ int c=fp_cmpv(fp_get(spec,dbl),AC[ac]);
		  fN=(c<0); fZ=(c==0); fV=0; fC=0; }
		return;
	case 0172400: AC[ac]=fp_get(spec,dbl); fp_setcc(AC[ac]); return;	/* movf LDF */
	case 0174000: fp_put(spec,AC[ac],dbl); fp_setcc(AC[ac]); return;	/* movf STF */
	case 0177400: AC[ac]=fp_get(spec,!dbl); fp_setcc(AC[ac]); return;	/* movof (load cvt) */
	case 0176000:	/* movfo (store cvt): D->F rounds at 24 bits */
		sv=AC[ac];
		if(dbl && sv.frac)	/* D mode: the converted store is F -> round at 24 */
			sv=fp_norm128(sv.sign,sv.exp,(u128fp)sv.frac<<64,24);
		fp_put(spec,sv,!dbl); fp_setcc(AC[ac]); return;
	case 0175000:	/* movei (STEXP): dst = unbiased exponent of AC[ac] */
		{ int e=(AC[ac].frac==0)?0:AC[ac].exp;
		  if(isreg) R[reg]=e&0xffff; else st2(operand(spec,0),e&0xffff); }
		return;
	case 0176400:	/* movie (LDEXP): AC[ac] = mantissa(AC[ac]) * 2^(int src) */
		{ int n=isreg?(short)R[reg]:(short)ld2(operand(spec,0));
		  if(AC[ac].frac) AC[ac].exp=n;
		  fN=(AC[ac].frac!=0)&&AC[ac].sign; fZ=(AC[ac].frac==0);
		  fV=(n>127||n<-127); fC=0; }
		return;
	case 0177000:	/* movif: int -> float (rounds at mode width) */
		if(isreg) iv=(short)R[reg];
		else { addr=fpi_addr(spec);
		       iv=(FPS&FPL)? (int)((ld2(addr)<<16)|ld2((addr+2)&0xffff))
				   : (short)ld2(addr); }
		AC[ac]=fp_fromlong(iv); fp_setcc(AC[ac]); return;
	case 0175400:	/* movfi: float -> int (truncate toward zero) */
		iv=fp_tolong(AC[ac]);
		if(isreg) R[reg]=iv&0xffff;
		else { addr=fpi_addr(spec);
		       if(FPS&FPL){ st2(addr,(iv>>16)&0xffff); st2((addr+2)&0xffff,iv&0xffff); }
		       else st2(addr,iv&0xffff); }
		fN=(AC[ac].frac!=0)&&AC[ac].sign; fZ=(AC[ac].frac==0); fV=0; fC=0; return;
	}
	fprintf(stderr,"apsim: unhandled fp instr %06o at %06o\n", instr, (PC-2)&0xffff);
	halted=1; ecode=127;
}

/* ---- late-hardware instructions ----------------------------------------
 * MFPT/SPL (11/23+..J-11), TSTSET/WRTLCK (J-11 MP), FIS (KEV11), and the
 * full Commercial Instruction Set (CISP/KEF74).  Semantics follow the DEC
 * CIS specification as encoded in simh's pdp11_cis.c: register forms take
 * descriptors in R0..R5, inline (I) forms fetch POINTER words to the
 * descriptors from the instruction stream (plus literal argument words);
 * inline forms leave the operand registers intact and set only result
 * registers.  Decimal strings run through a 128-bit binary engine (31
 * digits < 2^107).  CSM/MED/XFC stay reserved (user-mode runtime). */

static int cc_cmp16(int a,int b){	/* condition codes of CMP a,b */
	int r=(a-b)&0xffff;
	FZ=(r==0); FN=(r>>15)&1;
	FC=((a&0xffff)<(b&0xffff));
	FV=(((a^b)&(~b^r))>>15)&1;
	return r;
}


/* ---- CIS ---- */
typedef unsigned __int128 u128;
static int cis_inl;			/* current instruction is an inline form */
static void cis_dsc(int rlo,int *d0,int *d1){
	if(cis_inl){ int p=ifetch(PC); PC=(PC+2)&0xffff;
		*d0=ld2(p); *d1=ld2(p+2); }
	else { *d0=R[rlo]; *d1=R[rlo+1]; }
}
static int cis_arg(int rn){
	if(cis_inl){ int v=ifetch(PC); PC=(PC+2)&0xffff; return v; }
	return R[rn];
}

/* decimal string read: returns magnitude, sets *neg.  Descriptor word0:
 * type<14:12> (0 signed zoned, 1 unsigned zoned, 2/3 trailing/leading
 * overpunch, 4/5 trailing/leading separate, 6/7 signed/unsigned packed),
 * length<4:0> in digits. */
static u128 dec_read(int d0,int d1,int *neg){
	int ty=(d0>>12)&7, len=d0&037, a=d1&0xffff, i;
	u128 v=0;
	*neg=0;
	if(ty==6||ty==7){			/* packed: parse from the sign end */
		int nb=len/2+1, sn=ld1(a+nb-1)&0xf;
		u128 p10=1;
		if(ty==6 && (sn==0xB||sn==0xD)) *neg=1;
		for(i=0;i<len;i++){		/* digits right-to-left */
			int nib = (i&1)? ld1(a+nb-1-(i+1)/2)&0xf
				       : (ld1(a+nb-1-(i+1)/2)>>4)&0xf;
			v += (u128)(nib<=9?nib:0)*p10; p10*=10;
		}
		return v;
	}
	for(i=0;i<len;i++){			/* zoned/overpunch/separate: high digit first */
		int off = (ty==5)? 1 : 0;	/* leading separate: sign byte first */
		int c=ld1(a+off+i), dg=c&0xf;
		if(ty==2 && i==len-1){		/* trailing overpunch */
			if(c=='{'){dg=0;} else if(c>='A'&&c<='I'){dg=c-'A'+1;}
			else if(c=='}'){dg=0;*neg=1;} else if(c>='J'&&c<='R'){dg=c-'J'+1;*neg=1;}
		}
		if(ty==3 && i==0){		/* leading overpunch */
			if(c=='{'){dg=0;} else if(c>='A'&&c<='I'){dg=c-'A'+1;}
			else if(c=='}'){dg=0;*neg=1;} else if(c>='J'&&c<='R'){dg=c-'J'+1;*neg=1;}
		}
		if(ty==0 && i==len-1 && (c&0xf0)==0x70) *neg=1;	/* signed zoned: 7X zone */
		v=v*10+(dg<=9?dg:0);
	}
	if(ty==4 && ld1(a+len)=='-') *neg=1;		/* trailing separate */
	if(ty==5 && ld1(a)=='-') *neg=1;		/* leading separate */
	return v;
}
/* decimal write: truncates to the descriptor length; returns 1 on overflow */
static int dec_write(int d0,int d1,u128 v,int neg){
	int ty=(d0>>12)&7, len=d0&037, a=d1&0xffff, i, ovf=0;
	int dg[40];
	for(i=0;i<len;i++){ dg[i]=(int)(v%10); v/=10; }	/* dg[0]=LSD */
	if(v) ovf=1;
	if(ty==6||ty==7){
		int nb=len/2+1, sn = (ty==7)?0xF : (neg?0xD:0xC);
		unsigned char b[20];
		for(i=0;i<nb;i++) b[i]=0;
		b[nb-1]=sn;
		for(i=0;i<len;i++){
			if(i&1) b[nb-1-(i+1)/2] |= dg[i];
			else    b[nb-1-(i+1)/2] |= dg[i]<<4;
		}
		for(i=0;i<nb;i++) st1(a+i,b[i]);
		return ovf;
	}
	{ int off=(ty==5)?1:0;
	  for(i=0;i<len;i++) st1(a+off+i, 0x30|dg[len-1-i]);
	  if(ty==0 && neg && len) st1(a+off+len-1, 0x70|dg[0]);	/* signed zoned: 7X */
	  if(ty==2 && len){ int d=dg[0];		/* trailing overpunch */
		st1(a+len-1, neg? (d? 'J'+d-1 : '}') : (d? 'A'+d-1 : '{')); }
	  if(ty==3 && len){ int d=dg[len-1];		/* leading overpunch */
		st1(a, neg? (d? 'J'+d-1 : '}') : (d? 'A'+d-1 : '{')); }
	  if(ty==4) st1(a+len, neg?'-':'+');
	  if(ty==5) st1(a, neg?'-':'+');
	}
	return ovf;
}
static void dec_cc(u128 v,int neg,int ovf){
	FZ=(v==0); FN=(neg && v!=0); FV=ovf; FC=0;
}

static int cis(int instr)
{
	int lo=instr&0177;
	cis_inl=(lo&0100)!=0; lo&=077;
	if((lo&070)==020){				/* l2dr */
		int b=R[instr&7]&0xffff;
		R[0]=ld2(b); R[1]=ld2(b+2); R[2]=ld2(b+4); R[3]=ld2(b+6);
		return 1;
	}
	if((lo&070)==060){				/* l3dr */
		int b=R[instr&7]&0xffff;
		R[0]=ld2(b); R[1]=ld2(b+2); R[2]=ld2(b+4); R[3]=ld2(b+6);
		R[4]=ld2(b+8); R[5]=ld2(b+10);
		return 1;
	}
	switch(lo){
	case 030: case 031: case 032: {			/* movc/movrc/movtc */
		int s0,s1,d0,d1,fill,tbl=0,i,n;
		static unsigned char tmp[65536];
		cis_dsc(0,&s0,&s1); cis_dsc(2,&d0,&d1);
		fill=cis_arg(4)&0xff;
		if(lo==032){		/* movtc: translation table */
			if(cis_inl){ tbl=ifetch(PC); PC=(PC+2)&0xffff; }
			else tbl=R[5];
		}
		n = s0<d0 ? s0 : d0;
		for(i=0;i<n;i++){ int c=ld1(s1+i);
			if(lo==032) c=ld1((tbl+c)&0xffff);
			tmp[i]=c; }
		for(i=0;i<n;i++) st1(d1+i,tmp[i]);	/* buffered: overlap-safe */
		for(;i<d0;i++) st1(d1+i,fill);
		cc_cmp16(s0,d0);
		R[0]=(s0>d0)?(s0-d0)&0xffff:0;
		if(!cis_inl){ R[1]=0; R[2]=0; R[3]=0; }
		return 1; }
	case 040: case 041: {				/* locc/skpc */
		int s0,s1,ch,i;
		cis_dsc(0,&s0,&s1); ch=cis_arg(4)&0xff;
		for(i=0;i<s0;i++){
			int c=ld1(s1+i);
			if((lo==040 && c==ch) || (lo==041 && c!=ch)) break;
		}
		R[0]=(s0-i)&0xffff; R[1]=(s1+i)&0xffff;
		setNZ(R[0],0); FV=0; FC=0;
		return 1; }
	case 042: case 043: {				/* scanc/spanc */
		int s0,s1,mask,tbl,i;
		cis_dsc(0,&s0,&s1);
		if(cis_inl){ int t0,t1; cis_dsc(0,&t0,&t1); mask=t0&0xff; tbl=t1; }
		else { mask=R[4]&0xff; tbl=R[5]; }
		for(i=0;i<s0;i++){
			int e=ld1((tbl+ld1(s1+i))&0xffff)&mask;
			if((lo==042 && e) || (lo==043 && !e)) break;
		}
		R[0]=(s0-i)&0xffff; R[1]=(s1+i)&0xffff;
		setNZ(R[0],0); FV=0; FC=0;
		return 1; }
	case 044: {					/* cmpc */
		int s0,s1,d0,d1,fill,i,n,c1,c2;
		cis_dsc(0,&s0,&s1); cis_dsc(2,&d0,&d1);
		fill=cis_arg(4)&0xff;
		n = s0>d0 ? s0 : d0;
		c1=c2=0;
		for(i=0;i<n;i++){
			c1 = i<s0 ? ld1(s1+i) : fill;
			c2 = i<d0 ? ld1(d1+i) : fill;
			if(c1!=c2) break;
		}
		if(i==n){ FZ=1; FN=0; FV=0; FC=0;
			R[0]=0; R[1]=(s1+s0)&0xffff; R[2]=0; R[3]=(d1+d0)&0xffff; }
		else { int r=(c1-c2)&0xff;
			FZ=0; FN=(r>>7)&1; FC=(c1<c2); FV=(((c1^c2)&(~c2^r))>>7)&1;
			R[0]=(i<s0)?(s0-i)&0xffff:0; R[1]=(s1+(i<s0?i:s0))&0xffff;
			R[2]=(i<d0)?(d0-i)&0xffff:0; R[3]=(d1+(i<d0?i:d0))&0xffff; }
		return 1; }
	case 045: {					/* matc: find R2:R3 in R0:R1 */
		int s0,s1,o0,o1,i,j,found=-1;
		cis_dsc(0,&s0,&s1); cis_dsc(2,&o0,&o1);
		for(i=0; o0>0 && i+o0<=s0; i++){
			for(j=0;j<o0;j++) if(ld1(s1+i+j)!=ld1(o1+j)) break;
			if(j==o0){ found=i; break; }
		}
		if(found>=0){ R[0]=(s0-found)&0xffff; R[1]=(s1+found)&0xffff; }
		else { R[0]=0; R[1]=(s1+s0)&0xffff; }
		setNZ(R[0],0); FV=0; FC=0;
		return 1; }
	case 050: case 051: case 052:			/* addn/subn/cmpn */
	case 070: case 071: case 072: {			/* addp/subp/cmpp */
		int a0,a1,b0,b1,c0,c1,n1,n2,ovf;
		u128 v1,v2; __int128 s1v,s2v,r;
		cis_dsc(0,&a0,&a1); cis_dsc(2,&b0,&b1);
		v1=dec_read(a0,a1,&n1); v2=dec_read(b0,b1,&n2);
		s1v = n1? -(__int128)v1 : (__int128)v1;
		s2v = n2? -(__int128)v2 : (__int128)v2;
		if((lo&077)==052||(lo&077)==072){	/* cmpn/cmpp: src1 vs src2 */
			FZ=(s1v==s2v); FN=(s1v<s2v); FV=0; FC=0;
			return 1; }
		r = ((lo&077)==050||(lo&077)==070) ? s2v+s1v : s2v-s1v;
		cis_dsc(4,&c0,&c1);
		ovf=dec_write(c0,c1, r<0?(u128)(-r):(u128)r, r<0);
		dec_cc(r<0?(u128)(-r):(u128)r, r<0, ovf);
		return 1; }
	case 074: case 075: {				/* mulp/divp */
		int a0,a1,b0,b1,c0,c1,n1,n2,ovf;
		u128 v1,v2; __int128 s1v,s2v,r;
		cis_dsc(0,&a0,&a1); cis_dsc(2,&b0,&b1);
		v1=dec_read(a0,a1,&n1); v2=dec_read(b0,b1,&n2);
		s1v = n1? -(__int128)v1 : (__int128)v1;
		s2v = n2? -(__int128)v2 : (__int128)v2;
		cis_dsc(4,&c0,&c1);
		if(lo==075){
			if(s1v==0){ FC=1; FV=1; FZ=0; FN=0; return 1; }
			r = s2v/s1v;
		} else r = s1v*s2v;
		ovf=dec_write(c0,c1, r<0?(u128)(-r):(u128)r, r<0);
		dec_cc(r<0?(u128)(-r):(u128)r, r<0, ovf);
		if(lo!=075) FC=0;
		return 1; }
	case 056: case 076: {				/* ashn/ashp */
		int a0,a1,c0,c1,n1,ovf,arg,cnt,rnd,i;
		u128 v; __int128 sv;
		cis_dsc(0,&a0,&a1); cis_dsc(2,&c0,&c1);
		arg=cis_arg(4);
		cnt=(signed char)(arg&0xff); rnd=(arg>>8)&0xf;
		v=dec_read(a0,a1,&n1);
		if(cnt>0) for(i=0;i<cnt;i++) v*=10;
		else if(cnt<0){
			for(i=0;i<-cnt-1;i++) v/=10;
			if(-cnt>=1){ v=(v+rnd)/10; }
		}
		sv = n1? -(__int128)v : (__int128)v;
		ovf=dec_write(c0,c1,v,n1);
		dec_cc(v,n1,ovf); (void)sv;
		return 1; }
	case 053: case 073: {				/* cvtnl/cvtpl */
		int a0,a1,n1; u128 v; __int128 sv; long lres;
		cis_dsc(0,&a0,&a1);
		v=dec_read(a0,a1,&n1);
		sv = n1? -(__int128)v : (__int128)v;
		lres=(long)sv;
		FV=(sv>0x7fffffffL || sv< -0x80000000L);
		FZ=(sv==0); FN=(sv<0); FC=0;
		if(cis_inl){ int p=ifetch(PC); PC=(PC+2)&0xffff;
			st2(p,(lres>>16)&0xffff); st2(p+2,lres&0xffff); }
		else { R[2]=(lres>>16)&0xffff; R[3]=lres&0xffff; }
		return 1; }
	case 057: case 077: {				/* cvtln/cvtlp */
		int c0,c1,ovf; long lv;
		cis_dsc(0,&c0,&c1);			/* dst descriptor first */
		if(cis_inl){ int p=ifetch(PC); PC=(PC+2)&0xffff;
			lv=((long)ld2(p)<<16)|ld2(p+2); }	/* then src long addr */
		else lv=((long)R[2]<<16)|R[3];
		{ long m = (lv<0)? -lv : lv;
		  ovf=dec_write(c0,c1,(u128)(unsigned long)m, lv<0);
		  dec_cc((u128)(unsigned long)m, lv<0, ovf); }
		return 1; }
	case 054: case 055: {				/* cvtpn/cvtnp */
		int a0,a1,c0,c1,n1,ovf; u128 v;
		cis_dsc(0,&a0,&a1); cis_dsc(2,&c0,&c1);
		v=dec_read(a0,a1,&n1);
		ovf=dec_write(c0,c1,v,n1);
		dec_cc(v,n1,ovf);
		return 1; }
	}
	return 0;
}

/* dispatch for the late-hardware groups; returns 1 if the instruction
 * was handled */
static int late_insn(int instr)
{
	if(instr==7){ R[0]=5; return 1; }		/* mfpt: J-11 */
	if((instr&0177770)==0000230) return 1;		/* spl: user-mode no-op */
	if((instr&0177700)==0007200){			/* tstset */
		int d,old;
		if((instr&070)==0) return 0;		/* mode 0: reserved */
		d=operand(instr&077,0); old=getv(d,0);
		R[0]=old; putv(d,old|1,0);
		setNZ(old,0); FC=old&1; FV=0; return 1; }
	if((instr&0177700)==0007300){			/* wrtlck */
		int d;
		if((instr&070)==0) return 0;
		d=operand(instr&077,0); putv(d,R[0],0);
		setNZ(R[0],0); FV=0; return 1; }
	if((instr&0177740)==0075000){			/* fis (KEV11): stack F-format
					 * add/sub/mul/div, routed through the EXACT
					 * FP11 softfloat at F width -- not host doubles,
					 * so the last non-exact FP path is gone and the
					 * result matches FP11/fpsim to the bit.  A(R[r]+4)
					 * op B(R[r]) -> A; R[r] += 4 (pop B). */
		int r=instr&7, savefps=FPS, addr=(R[r]+4)&0xffff;
		struct fpv a,b,res;
		FPS &= ~FPD;			/* single precision: round at 24 bits */
		b=rdfloat(R[r],0); a=rdfloat(addr,0);
		switch((instr>>3)&3){
		case 0: res=fp_addv(a,b); break;
		case 1: res=fp_addv(a,fp_negv(b)); break;
		case 2: res=fp_mulv(a,b); break;
		default: res=(b.frac==0)?fp_zero():fp_divv(a,b); break;	/* div0: lenient */
		}
		wrfloat(addr,res,0);
		FPS=savefps;
		R[r]=addr;
		FN=(res.frac!=0)&&res.sign; FZ=(res.frac==0); FV=0; FC=0; return 1; }
	if((instr&0177600)==0076000 && (instr&0177)!=0100)	/* cis */
		return cis(instr);
	if(instr==1) return 1;				/* wait: user-mode no-op */
	if(instr==5) return 1;				/* reset: user-mode no-op */
	if((instr&0177700)==0006400){			/* mark nn: SP=PC+2nn,
							 * PC=R5, R5=(SP)+ */
		SP=(PC+2*(instr&077))&0xffff;
		PC=R[5]&0xffff;
		R[5]=ld2(SP); SP=(SP+2)&0xffff;
		return 1; }
	if((instr&0077700)==0006500){			/* mfpi/mfpd: previous
							 * mode is user -- push (src) */
		int d=operand(instr&077,0), v=getv(d,0);
		SP=(SP-2)&0xffff; st2(SP,v);
		setNZ(v,0); FV=0; return 1; }
	if((instr&0077700)==0006600){			/* mtpi/mtpd: pop -> dst */
		int v=ld2(SP), d;
		SP=(SP+2)&0xffff;
		d=operand(instr&077,0); putv(d,v,0);
		setNZ(v,0); FV=0; return 1; }
	if((instr&0177700)==0106700){			/* mfps: PS low byte */
		int ps=(FN<<3)|(FZ<<2)|(FV<<1)|FC, d=operand(instr&077,1);
		if(d&ISREG) R[d&7]=ps;			/* sign-extend n/a: ps>=0 */
		else putv(d,ps,1);
		setNZ(ps,1); FV=0; return 1; }
	if((instr&0177700)==0106400){			/* mtps: set CC from src
							 * byte (priority ignored
							 * in user mode) */
		int d=operand(instr&077,1), v=getv(d,1);
		FN=(v>>3)&1; FZ=(v>>2)&1; FV=(v>>1)&1; FC=v&1;
		return 1; }
	return 0;					/* csm/med/xfc: reserved */
}

static void step(void)
{
	int instr=ifetch(PC), op, byte, s, d, sv, dv, r, sl, dl;
	PC=(PC+2)&0xffff;

	/* branches first (their high bits overlap other encodings) */
	if(((instr&0177400)>=0000400 && (instr&0177400)<=0003400) ||
	   ((instr&0177400)>=0100000 && (instr&0177400)<=0103400)){
		int c=cond(instr);
		if(c>=0){ if(c){ int off=(signed char)(instr&0377); PC=(PC+2*off)&0xffff; } return; }
	}
	/* sys / trap */
	if((instr&0177400)==0104400){ do_sys(instr); return; }	/* sys/trap (0104400|n) */
	if((instr&0177400)==0104000){				/* EMT (0104000|n) */
		if(v1sys){	/* V1's rtssym: `emt n' is a kernel-VALIDATED
				 * `rts rn' -- the 1971 trap handler checks the
				 * return address (in core, even, non-null) and
				 * bounces through a trampoline of real rts
				 * instructions.  1971 binaries return from
				 * subroutines this way (chown does). */
			int n=instr&7, t;
			t = (n==7) ? PC : R[n];
			if((t&1) || ld2(t)==0){		/* badrts: fall through as a nop
							 * (the real kernel resumed the program) */
				fprintf(stderr,"apsim: V1 emt %d bad return %06o at pc=%06o\n",n,t,(PC-2)&0xffff);
				halted=1; ecode=128+7; return;
			}
			if(n!=7){ R[n]=ld2(SP); SP=(SP+2)&0xffff; PC=t&0xffff; }
			else { PC=ld2(SP); SP=(SP+2)&0xffff; }
			return;
		}
		if(ov_proc){	/* 0430/0431: overlay switch, ovno in r0 (csv.s
				 * ovhndlr protocol); 0431 windows live in I-space */
			int n=R[0];
			unsigned char *win = ov_sep ? MI : M;
			if(n>=1&&n<=15&&ov_siz[n]>0){
				memset(win+ov_base,0,ov_max);
				memcpy(win+ov_base,ov_img[n-1],ov_siz[n]);
				cur_ovno=n;
				if(systrace) fprintf(stderr,"apsim: overlay -> %d (%d bytes @%06o%s)\n",n,ov_siz[n],ov_base,ov_sep?" I":"");
			} else {
				fprintf(stderr,"apsim: EMT bad overlay %d\n",n);
				halted=1; ecode=128+7;
			}
			return;
		}
		if(raise_fault(7)) return;			/* -> SIGEMT */
		halted=1; ecode=128+7; return;
	}
	/* late-hardware groups (mfpt/spl/tstset/wrtlck/fis/cis) */
	if(late_insn(instr)) return;

	op=(instr>>12)&017;
	byte=(op>=011 && op<=015);		/* MOVB..BISB carry bit 15 */

	/* FP11 floating-point group (017xxxx) */
	if(op==017){ do_fp(instr); return; }

	/* double-operand: MOV1 CMP2 BIT3 BIC4 BIS5 ADD6 ; +010 byte ; 016=SUB */
	if((op>=1&&op<=6)||(op>=011&&op<=016)){
		int bop = op&07;		/* 1..6, or 6 for SUB(016) */
		s=operand((instr>>6)&077,byte); sv=getv(s,byte);
		d=operand(instr&077,byte);
		if(op==016){ /* SUB (word only) */
			dv=getv(d,0);
			r=(dv-sv)&0xffff; putv(d,r,0); setNZ(r,0);
			FC=((unsigned)(dv&0xffff) < (unsigned)(sv&0xffff));
			FV=(((dv^sv)&(~sv^r))>>15)&1; return;
		}
		switch(bop){
		case 1:							/* MOV/MOVB */
			/* MOVB to a register is the one byte instruction that
			 * sign-extends the byte into the high half of the register
			 * (all others leave the high byte alone).  Without this, a
			 * `movb' of e.g. a NUL leaves stale high bits, so the format
			 * loop's `beq' on a NUL never fires (printf %d hung). */
			if(byte && (d&ISREG)) R[d&7]=sgn(sv,1)&0xffff;
			else putv(d,sv,byte);
			setNZ(sv,byte); FV=0; break;
		case 2: dv=getv(d,byte); r=(sv-dv);			/* CMP */
			setNZ(r,byte); { int m=byte?0xff:0xffff,sb=byte?0x80:0x8000;
			FC=((sv&m)<(dv&m)); FV=(((sv^dv)&(~dv^r))&sb)!=0; } break;
		case 3: dv=getv(d,byte); r=sv&dv; setNZ(r,byte); FV=0; break;	/* BIT */
		case 4: dv=getv(d,byte); r=dv&~sv; putv(d,r,byte); setNZ(r,byte); FV=0; break; /* BIC */
		case 5: dv=getv(d,byte); r=dv|sv; putv(d,r,byte); setNZ(r,byte); FV=0; break; /* BIS */
		case 6: dv=getv(d,0); r=(dv+sv)&0xffff; putv(d,r,0); setNZ(r,0);	/* ADD */
			FC=((unsigned)(dv&0xffff)+(unsigned)(sv&0xffff))>0xffff;
			FV=((~(dv^sv)&(dv^r))>>15)&1; break;
		}
		return;
	}

	/* EIS / xor: 07RSS -- MUL DIV ASH ASHC XOR */
	if(op==07){
		int reg=(instr>>6)&7;
		switch(instr&0177000){
		case 0070000:	/* MUL: R*src -> R,R|1 (32-bit) */
			s=operand(instr&077,0); sv=(short)getv(s,0);
			{ long p=(long)(short)R[reg]*sv; R[reg]=(p>>16)&0xffff; R[reg|1]=p&0xffff;
			  FZ=(p==0); FN=(p<0); FV=0; FC=(p<-32768||p>32767); }
			return;
		case 0071000:	/* DIV: the R,R|1 pair is a SIGNED 32-bit dividend --
				 * sign-extend it, or negative dividends (timezone
				 * offsets, pointer differences) divide as huge
				 * positives (found via 2.11 ls/date going insane). */
			s=operand(instr&077,0); sv=(short)getv(s,0);
			if(sv==0){ FV=FC=1; return; }
			{ long dd=(long)(int32_t)(((uint32_t)R[reg]<<16)|R[reg|1]);
			  long q=dd/sv, rm=dd%sv;
			  if(q>32767||q<-32768){ FV=1; FC=0; return; }  /* overflow: regs unchanged */
			  R[reg]=q&0xffff; R[reg|1]=rm&0xffff;
			  FZ=(q==0); FN=(q<0); FV=0; FC=0; }
			return;
		case 0072000:	/* ASH: shift R by src (signed count) */
			s=operand(instr&077,0); sv=(short)getv(s,0);
			{ int cnt=sv&077; if(cnt&040)cnt-=64; int val=(short)R[reg];
			  if(cnt>=0) val<<=cnt; else val>>=(-cnt);
			  R[reg]=val&0xffff; setNZ(R[reg],0); FV=0; }
			return;
		case 0073000:	/* ASHC: the pair is a SIGNED 32-bit value; right
				 * shifts are ARITHMETIC (sign propagates).  Building
				 * it unsigned shifted zeros into negative values --
				 * the bug that scrambled 2.11 ls/date (long division
				 * helpers lean on ashc). */
			s=operand(instr&077,0); sv=(short)getv(s,0);
			{ int cnt=sv&077; if(cnt&040)cnt-=64;
			  long val=(long)(int32_t)(((uint32_t)R[reg]<<16)|R[reg|1]);
			  if(cnt>0){ FC=(cnt<=32)?((val>>(32-cnt))&1):0; val<<=cnt; }
			  else if(cnt<0){ FC=(val>>((-cnt)-1))&1; val>>=(-cnt); }
			  val=(long)(int32_t)(val&0xffffffffL);
			  R[reg]=((unsigned long)val>>16)&0xffff; R[reg|1]=val&0xffff;
			  FZ=(val==0); FN=(val<0); FV=0; }
			return;
		case 0074000:	/* XOR: R ^ dst -> dst */
			d=operand(instr&077,0); dv=getv(d,0); r=dv^R[reg];
			putv(d,r,0); setNZ(r,0); FV=0;
			return;
		}
	}

	/* JSR: 004RDD */
	if((instr&0177000)==0004000){
		int reg=(instr>>6)&7;
		d=operand(instr&077,0);			/* dst = target address */
		SP=(SP-2)&0xffff; st2(SP,R[reg]);	/* push reg */
		R[reg]=PC; PC=d&0xffff;
		return;
	}
	/* RTS: 00020R */
	if((instr&0177770)==0000200){
		int reg=instr&7; PC=R[reg]; R[reg]=ld2(SP); SP=(SP+2)&0xffff; return;
	}
	/* SOB: 077Rnn (decrement reg, branch back if nonzero) */
	if((instr&0177000)==0077000){
		int reg=(instr>>6)&7, off=instr&077;
		R[reg]=(R[reg]-1)&0xffff;
		if(R[reg]!=0) PC=(PC-2*off)&0xffff;
		return;
	}

	/* single-operand (word/byte): 0?05DDDD..0?063DDD, JMP, SWAB, SXT */
	{
		int sop=instr&0177700, b=(instr&0100000)!=0;
		int spec=instr&077;
		switch(instr&0077700){		/* ignore bit15 (byte) for the group */
		case 0005000: d=operand(spec,b); r=0; putv(d,0,b); FN=0;FZ=1;FV=0;FC=0; return;	/* CLR */
		case 0005100: d=operand(spec,b); dv=getv(d,b); r=~dv; putv(d,r,b); setNZ(r,b); FV=0;FC=1; return; /* COM */
		case 0005200: d=operand(spec,b); dv=getv(d,b); r=dv+1; putv(d,r,b); setNZ(r,b);	/* INC */
			FV=((b?(dv&0xff)==0x7f:(dv&0xffff)==0x7fff)); return;
		case 0005300: d=operand(spec,b); dv=getv(d,b); r=dv-1; putv(d,r,b); setNZ(r,b);	/* DEC */
			FV=((b?(dv&0xff)==0x80:(dv&0xffff)==0x8000)); return;
		case 0005400: d=operand(spec,b); dv=getv(d,b); r=-dv; putv(d,r,b); setNZ(r,b);	/* NEG */
			FC=((r&(b?0xff:0xffff))!=0); FV=0; return;
		case 0005500: d=operand(spec,b); dv=getv(d,b); r=dv+FC; putv(d,r,b); setNZ(r,b); return;	/* ADC */
		case 0005600: d=operand(spec,b); dv=getv(d,b); r=dv-FC; putv(d,r,b); setNZ(r,b); return;	/* SBC */
		case 0005700: d=operand(spec,b); dv=getv(d,b); setNZ(dv,b); FV=0;FC=0; return;	/* TST */
		case 0006000: d=operand(spec,b); dv=getv(d,b);	/* ROR */
			{ int m=b?0xff:0xffff,nb=b?0x80:0x8000; r=(dv>>1)|(FC?nb:0); FC=dv&1;
			  putv(d,r,b); setNZ(r,b); FV=FN^FC; } return;
		case 0006100: d=operand(spec,b); dv=getv(d,b);	/* ROL */
			{ int m=b?0xff:0xffff,nb=b?0x80:0x8000; r=((dv<<1)|(FC?1:0))&m; FC=(dv&nb)!=0;
			  putv(d,r,b); setNZ(r,b); FV=FN^FC; } return;
		case 0006200: d=operand(spec,b); dv=getv(d,b);	/* ASR */
			{ int nb=b?0x80:0x8000; FC=dv&1; r=(sgn(dv,b)>>1); putv(d,r,b); setNZ(r,b); FV=FN^FC; } return;
		case 0006300: d=operand(spec,b); dv=getv(d,b);	/* ASL */
			{ int m=b?0xff:0xffff,nb=b?0x80:0x8000; r=(dv<<1)&m; FC=(dv&nb)!=0; putv(d,r,b); setNZ(r,b); FV=FN^FC; } return;
		}
		switch(instr&0177700){
		case 0000100: d=operand(spec,0); PC=d&0xffff; return;		/* JMP */
		case 0000300: d=operand(spec,0); dv=getv(d,0);			/* SWAB */
			r=((dv<<8)|((dv>>8)&0xff))&0xffff; putv(d,r,0); FZ=((r&0xff)==0); FN=((r&0x80)!=0); FV=0;FC=0; return;
		case 0006700: d=operand(spec,0); putv(d,FN?0xffff:0,0); FZ=!FN; FV=0; return; /* SXT */
		}
	}

	/* trap-family + misc instructions */
	switch(instr){
	case 0000000: halted=1; ecode=0; return;	/* HALT */
	case 0000002:					/* RTI */
	case 0000006: {					/* RTT: pop PC then PS (signal */
		int npc=ld2(SP), ps=ld2((SP+2)&0xffff);	/* return -- sendsig's frame) */
		SP=(SP+4)&0xffff; PC=npc;
		FC=ps&1; FV=(ps>>1)&1; FZ=(ps>>2)&1; FN=(ps>>3)&1;
		return;
	}
	case 0000003:					/* BPT -> SIGTRAP (adb breakpoints) */
		if(raise_fault(5)) return;
		fprintf(stderr,"apsim: BPT at %06o (no SIGTRAP handler)\n",(PC-2)&0xffff);
		halted=1; ecode=128+5; return;
	case 0000004:					/* IOT -> SIGIOT (abort()) */
		if(raise_fault(6)) return;
		halted=1; ecode=128+6; return;
	}
	/* condition-code set/clear (000240-000277): bits 0-3 pick flags
	 * (C=1,V=2,Z=4,N=8), bit 4 (020) = set vs clear.  NOP (000240) sets
	 * none.  as uses SEV (000262) to mark EOF -- treating it as a no-op
	 * left V unset and looped getw forever. */
	if((instr&0177740)==0000240){
		int set=(instr&020)!=0;
		if(instr&001) FC=set;
		if(instr&002) FV=set;
		if(instr&004) FZ=set;
		if(instr&010) FN=set;
		return;
	}
	if((instr&0177400)==0170000) return;		/* FP control (setd/setf/seti/setl/cfcc...) */
	if((instr&0177000)>=0170000) return;		/* other FP: ignore */

	/* unknown opcode -> SIGILL.  If the guest installed a handler, deliver it
	 * (faithful: 2.8 traps illegal instructions to SIGILL).  If not, this is
	 * almost always an apsim DECODE GAP rather than a guest bug, so print the
	 * diagnostic and halt -- keeping emulation holes visible. */
	if(raise_fault(4)) return;
	fprintf(stderr,"apsim: illegal instruction %06o at %06o\n", instr, (PC-2)&0xffff);
	{ int a; fprintf(stderr,"  arena window 0111130..0111154:"); for(a=0111130;a<=0111154;a+=2) fprintf(stderr," %06o:%06o",a,ld2(a)); fprintf(stderr,"\n");
	  fprintf(stderr,"  recent pcs:"); { int k; for(k=0;k<16;k++) fprintf(stderr," %06o",pcring[(pcri+k)&15]); } fprintf(stderr,"\n"); }
	halted=1; ecode=126;
}

/* ---- program loading, shared by the initial load and exec(2) ---- *
 * A guest absolute path is rooted under $APSIM_ROOT when set (a faux 2.8 tree,
 * like Apout's APOUT_ROOT) so binaries that exec a hardcoded /usr/ucb/lib/...
 * and read/write /tmp/... resolve inside the sandbox.  Unset -> raw path
 * (unchanged behaviour for c0/c1/c2/ld). */
static char *mappath_s(const char *gp){
	static char buf[1024];
	char *root=getenv("APSIM_ROOT");
	if(root && gp[0]=='/') snprintf(buf,sizeof buf,"%s%s",root,gp);
	else snprintf(buf,sizeof buf,"%s",gp);
	return buf;
}
static char *mappath(int gaddr){
	return mappath_s((char*)(M+(gaddr&0xffff)));
}
/* The guest-visible spelling of a host path: strip the $APSIM_ROOT prefix
 * a command-line path may carry, so an interpreter can re-open the script
 * through its own rooted open(). */
static const char *guestify(const char *hp){
	char *root=getenv("APSIM_ROOT"); size_t n;
	if(root && (n=strlen(root))>1 && !strncmp(hp,root,n) && hp[n]=='/')
		return hp+n;
	return hp;
}
/* lay out the exec stack 2.8 crt0 expects: sp -> argc, argv[], NULL, envp[],
 * NULL, then the arg+env strings near the top of D-space.  If `env' is NULL the
 * environment comes from $APSIM_ENV (space-separated VAR=VAL) else a sane
 * default, so curses programs find $TERM; exece() passes the guest's own env. */
static void setup_stack(int nargs, char **args, int nenv, char **env){
	static char envbuf[512]; char *envv[32]; int k, straddr=0177700, sa;
	if(env){ for(k=0;k<nenv && k<32;k++) envv[k]=env[k]; if(nenv>32)nenv=32; }
	else {	char *e=getenv("APSIM_ENV"); char *p;
		strncpy(envbuf, e?e:"TERM=ansi HOME=/ PATH=/bin:/usr/bin", sizeof envbuf-1);
		envbuf[sizeof envbuf-1]=0; nenv=0;
		p=envbuf; while(*p && nenv<31){ while(*p==' ')p++; if(!*p)break;
		   envv[nenv++]=p; while(*p && *p!=' ')p++; if(*p)*p++=0; } }
	for(k=0;k<nargs;k++) straddr-=strlen(args[k])+1;
	for(k=0;k<nenv;k++)  straddr-=strlen(envv[k])+1;
	straddr&=~1;
	SP=straddr-2*(nargs+nenv+3);
	st2(SP,nargs);
	sa=straddr;
	for(k=0;k<nargs;k++){ st2(SP+2+2*k,sa); strcpy((char*)M+sa,args[k]); sa+=strlen(args[k])+1; }
	st2(SP+2+2*nargs,0);				/* argv NULL */
	for(k=0;k<nenv;k++){ st2(SP+4+2*nargs+2*k,sa); strcpy((char*)M+sa,envv[k]); sa+=strlen(envv[k])+1; }
	st2(SP+4+2*nargs+2*nenv,0);			/* envp NULL */
}
/* the First Edition exec stack: strings at the top of user core, then a -1
 * terminator, the argv pointers, and argc at sp.  No environment existed in
 * 1971.  User core was 040000..060000 (8K words), so argument POINTERS are
 * positive 16-bit values -- programs rely on that: ar's copfl walks its
 * member list with `tst (r1)+; blt skip', marking done entries by their
 * sign bit.  Args near 0177xxx (apout's choice) silently break it. */
#define V1TOP 060000
static void setup_stack_v1(int nargs, char **args){
	int k, posn=V1TOP-2, aposn[32];
	if(nargs>32) nargs=32;
	for(k=nargs-1;k>=0;k--){ int len=strlen(args[k])+1;
		posn-=len; memcpy(M+posn,args[k],len); aposn[k]=posn; }
	posn&=~1;
	posn-=2; st2(posn,0177777);			/* -1 ends the arg vector */
	for(k=nargs-1;k>=0;k--){ posn-=2; st2(posn,aposn[k]); }
	posn-=2; st2(posn,nargs);
	SP=posn;
}
/* load a 2.8 a.out into the (cleared) address space and set PC.  args are host
 * copies of the guest argv (the old image is wiped, so they must be copied out
 * before calling).  Returns -1 if the file can't be opened / has bad magic.
 * load_aout_env takes an explicit environment (for exece); load_aout uses the
 * default ($APSIM_ENV). */
/* Interpreter scripts.  "#!" execs the named interpreter with argv
 * rewritten to [interp, optional-arg, scriptpath, original args 1..] --
 * one optional argument and one level of interpretation, the classic
 * kernel rules (an interpreter that is itself a script fails).  A
 * shebang-less TEXT file given on apsim's own command line gets the
 * execvp courtesy -- run through the guest's /bin/sh -- because there is
 * no calling shell to do the ENOEXEC fallback (2.11's /bin/true is
 * literally the two bytes "exit 0"); a GUEST exec of such a file still
 * fails, authentically, so guest shells apply their own fallback. */
static int sb_depth;		/* one level of script interpretation */
static int initial_exec;	/* set for the command-line load only */
static int load_script(FILE *f, const char *path, int use_line,
		       int nargs, char **args, int nenv, char **env){
	static char line[256], interp[256], iarg[256], spath[1024];
	static char *nav[72];
	char *p, *q; int na=0, k, r;
	if(sb_depth){ fclose(f); return -1; }
	interp[0]=iarg[0]=0;
	if(use_line){			/* parse "#! interp [arg]" */
		fseek(f,2,SEEK_SET);
		if(!fgets(line,sizeof line,f)){ fclose(f); return -1; }
		line[strcspn(line,"\n")]=0;
		p=line; while(*p==' '||*p=='\t')p++;
		if(!*p){ fclose(f); return -1; }
		q=p; while(*q&&*q!=' '&&*q!='\t')q++;
		snprintf(interp,sizeof interp,"%.*s",(int)(q-p),p);
		while(*q==' '||*q=='\t')q++;
		for(p=q+strlen(q); p>q && (p[-1]==' '||p[-1]=='\t'); p--) ;
		if(p>q) snprintf(iarg,sizeof iarg,"%.*s",(int)(p-q),q);
	} else
		strcpy(interp,"/bin/sh");
	fclose(f);
	snprintf(spath,sizeof spath,"%s",guestify(path));
	nav[na++]=interp;
	if(iarg[0]) nav[na++]=iarg;
	nav[na++]=spath;
	for(k=1;k<nargs&&na<70;k++) nav[na++]=args[k];
	sb_depth=1;
	r=load_aout_env(mappath_s(interp), na, nav, nenv, env);
	sb_depth=0;
	return r;
}
static int load_aout_env(const char *path, int nargs, char **args, int nenv, char **env){
	FILE *f=fopen(path,"rb"); int hdr[8],i,tsize,dsize,entry;
	if(!f) return -1;
	for(i=0;i<8;i++){ int lo=fgetc(f),hi=fgetc(f); hdr[i]=lo|(hi<<8); }
	if((hdr[0]&0xffff)==0x2123)	/* "#!" */
		return load_script(f, path, 1, nargs, args, nenv, env);
	if(hdr[0]!=0405&&hdr[0]!=0407&&hdr[0]!=0410&&hdr[0]!=0411&&hdr[0]!=0430&&hdr[0]!=0431){
		int b0=hdr[0]&0xff;
		if(initial_exec && (b0=='\n'||b0=='\t'||(b0>=' '&&b0<127)))
			return load_script(f, path, 0, nargs, args, nenv, env);
		fclose(f); return -1;
	}
	if(hdr[0]==0405){
		/* First Edition: [0405][a_text incl header][syms][reloc][data area][0].
		 * exec loads the whole FILE IMAGE (12-byte header included) at core
		 * 040000 and enters at 040000: the magic word is `br .+14', hopping
		 * its own header.  The `data area' is zero space above the text. */
		v1sys=1; ov_proc=0; tsizew=0; Isp=M;
		memset(M,0,1<<16);
		fseek(f,0,SEEK_SET);
		if(fread(M+040000,1,(hdr[1]&0xffff)<=0100000?(hdr[1]&0xffff):0100000,f)){}
		fclose(f);
		setup_stack_v1(nargs,args);
		PC=040000;
		return 0;
	}
	tsize=hdr[1]; dsize=hdr[2]; entry=hdr[5];
	ov_proc=0; ov_sep=0; cur_ovno=0;
	if(hdr[0]==0430 || hdr[0]==0431){
		/* Auto-overlay executables: exec + ovlhdr{max_ovl, ov_siz[NOVL]},
		 * then base text, overlay texts, data.  NOVL is 7 through 2.9
		 * (32-byte header) and 15 from 2.10 on (48-byte header, gated on
		 * the universe).  0430 shares one space: overlay window at
		 * (tsize+017777)&~017777, data above it.  0431 is separate I&D:
		 * base text AND the overlay window live in I-space, data at D:0
		 * -- how 2.11 fits csh (55K base + 7K window + 10K data). */
		int novl=(Kern>=PDP11_K_BSD210)?15:7;
		int ovh[16],j,dbase;
		for(j=0;j<novl+1;j++){ int lo=fgetc(f),hi=fgetc(f); ovh[j]=lo|(hi<<8); }
		ov_max=ovh[0];
		if(ov_max>16384){ fclose(f); return -1; }
		ov_sep=(hdr[0]==0431);
		ov_base=(tsize+017777)&~017777;
		if(ov_base+ov_max>0x10000){ fclose(f); return -1; }
		for(j=1;j<=novl;j++) ov_siz[j]=ovh[j];
		for(;j<16;j++) ov_siz[j]=0;
		if(ov_sep){
			Isp=MI; tsizew=0;
			memset(MI,0,1<<16); memset(M,0,1<<16);
			if(fread(MI,1,tsize,f)){}
			for(j=1;j<=novl;j++)
				if(ov_siz[j]>0){ if(fread(ov_img[j-1],1,ov_siz[j],f)){} }
			if(fread(M,1,dsize,f)){}
			gbrk=(dsize+hdr[3])&0xffff;
		} else {
			dbase=((ov_base+ov_max)+017777)&~017777;
			tsizew=0; Isp=M;
			memset(M,0,1<<16);
			if(fread(M,1,tsize,f)){}
			for(j=1;j<=novl;j++)
				if(ov_siz[j]>0){ if(fread(ov_img[j-1],1,ov_siz[j],f)){} }
			if(fread(M+dbase,1,dsize,f)){}
			gbrk=(dbase+dsize+hdr[3])&0xffff;
		}
		ov_proc=1;
	} else if(hdr[0]==0411){
		Isp=MI; tsizew=0;
		memset(MI,0,1<<16); memset(M,0,1<<16);
		if(fread(MI,1,tsize,f)){} if(fread(M,1,dsize,f)){}
	} else {
		int dbase=(hdr[0]==0407)?tsize:((tsize+017777)&~017777);
		if(getenv("APSIM_DBASE")) dbase=(int)strtol(getenv("APSIM_DBASE"),0,8);
		tsizew=(hdr[0]==0407)?0:tsize; Isp=M;
		memset(M,0,1<<16);
		if(fread(M,1,tsize,f)){} if(fread(M+dbase,1,dsize,f)){}
		gbrk=(dbase+dsize+hdr[3])&0xffff;
	}
	if(hdr[0]==0411) gbrk=(dsize+hdr[3])&0xffff;
	fclose(f);
	setup_stack(nargs,args,nenv,env);
	PC=entry;
	return 0;
}
static int load_aout(const char *path, int nargs, char **args){
	return load_aout_env(path, nargs, args, 0, 0);
}

int main(int argc, char **argv)
{
	FILE *f; int hdr[8], ai=1, tsize, dsize, bsize, entry; long long i;
	/* env first, so a command-line flag can override it */
	if(getenv("APSIM_WATCHTEXT")) watchtext=atoi(getenv("APSIM_WATCHTEXT"));
	if(getenv("APSIM_WATCHSP")) watchsp=1;
	if(getenv("APSIM_WATCHADDR")) watchaddr=(int)strtol(getenv("APSIM_WATCHADDR"),0,8);
	if(getenv("APSIM_WATCHVAL")) watchval=(int)strtol(getenv("APSIM_WATCHVAL"),0,8);
	if(getenv("APSIM_UID")){ g_ruid=g_euid=atoi(getenv("APSIM_UID")); }
	if(getenv("APSIM_GID")){ g_rgid=g_egid=atoi(getenv("APSIM_GID")); }
	if(getenv("APSIM_PID")){ g_fakepid=atoi(getenv("APSIM_PID")); }
	/* universe: $PDP11_UNIVERSE first, then --universe/-u override */
	{ char *e=getenv("PDP11_UNIVERSE");
	  if(e && *e && !univ_apply(e)){
		fprintf(stderr,"apsim: unknown universe `%s' in PDP11_UNIVERSE; valid:",e);
#define X(n,i,s,k,d) fprintf(stderr," %s",n);
		PDP11_UNIVERSE_TABLE(X)
#undef X
		fprintf(stderr,"\n"); return 2; } }
	if(getenv("APSIM_SYSARGS") && !strcmp(getenv("APSIM_SYSARGS"),"stack")) stackargs=1;
	/* flags (any order): -t trace instrs, -s trace syscalls, -p N fix getpid() */
	while(ai<argc && argv[ai][0]=='-' && argv[ai][1]){
		if(!strcmp(argv[ai],"-t")) { trace=1; ai++; }
		else if(!strcmp(argv[ai],"-s")) { systrace=1; ai++; }
		else if(!strcmp(argv[ai],"-2")) { stackargs=1; ai++; }	/* stack-arg syscalls override */
		else if(!strcmp(argv[ai],"-p") && ai+1<argc) { g_fakepid=atoi(argv[ai+1]); ai+=2; }
		else if(!strncmp(argv[ai],"--universe=",11) || !strcmp(argv[ai],"-u") || !strcmp(argv[ai],"--universe")) {
			const char *nm;
			if(argv[ai][1]=='-' && argv[ai][10]=='='){ nm=argv[ai]+11; ai++; }
			else { if(ai+1>=argc){ fprintf(stderr,"apsim: %s needs a name\n",argv[ai]); return 2; } nm=argv[ai+1]; ai+=2; }
			if(!univ_apply(nm)){
				fprintf(stderr,"apsim: unknown universe `%s'; valid:",nm);
#define X(n,i,s,k,d) fprintf(stderr," %s",n);
				PDP11_UNIVERSE_TABLE(X)
#undef X
				fprintf(stderr,"\n"); return 2;
			}
		}
		else break;
	}
	if(ai>=argc){ fprintf(stderr,"usage: apsim [-t] [-s] [-p pid] [-u universe] [-2] a.out [args]\n"); return 2; }
	/* save the host terminal so a curses guest's raw/cbreak mode can be applied
	 * and restored.  No-op when stdin isn't a tty (e.g. piped test input). */
	if(isatty(0) && tcgetattr(0,&saved_tio)==0){ tty_saved=1; atexit(tty_restore); }
	(void)f; (void)hdr; (void)tsize; (void)dsize; (void)bsize; (void)entry;
	/* load via the shared loader (handles 0407/0410/0411, stack, env, PC).
	 * Guest argv = host argv past the a.out path. */
	pid_learn(getpid()); pid_learn(getppid());
	initial_exec=1;
	i=load_aout(argv[ai], argc-ai, argv+ai);
	initial_exec=0;
	if(i < 0){
		fprintf(stderr,"apsim: cannot load %s\n", argv[ai]); return 2; }

	for(i=0; i<4000000000LL && !halted; i++){
		if(pending_sig && !((guest_sigmask>>(pending_sig-1))&1)){
			/* deliver a caught signal, 2.8 sendsig() (a blocked
			 * signal stays pending until sigsetmask releases it) */
			int s=pending_sig, h, hs; pending_sig=0;
			if(s>0 && s<32 && (h=guest_sigh[s])>1 && guest_reliable[s]){
				/* 4.3-style delivery: build the 26-byte sigframe
				 * (signum, code, scp, sigcontext) the kernel's
				 * sendsig() leaves, enter sigtramp with r0=handler,
				 * and block sig+hmask until sigreturn restores.
				 * Frame layout from pdp/machdep.c sendsig(). */
				int ps = (FC) | (FV<<1) | (FZ<<2) | (FN<<3);
				int n = (SP-26)&0xffff;
				int scp = (n+6)&0xffff;
				st2(n+0, s);			/* sf_signum */
				st2(n+2, 0);			/* sf_code   */
				st2(n+4, scp);			/* sf_scp    */
				st2(scp+0, 0);			/* sc_onstack */
				stl(scp+2, guest_sigmask);	/* sc_mask (long) */
				st2(scp+6, SP);			/* sc_sp */
				st2(scp+8, R[5]);		/* sc_fp */
				st2(scp+10, R[1]);		/* sc_r1 */
				st2(scp+12, R[0]);		/* sc_r0 */
				st2(scp+14, PC);		/* sc_pc */
				st2(scp+16, ps);		/* sc_ps */
				st2(scp+18, ov_proc?cur_ovno:0);	/* sc_ovno: the overlay
							 * mapped when the signal hit, so
							 * sigreturn restores it (else it
							 * emt's a garbage number -> SIGEMT) */
				guest_sigmask |= guest_hmask[s] | ((long)1<<(s-1));
				R[0]=h;				/* handler addr, for sigtramp jsr (r0) */
				SP=n;
				PC=guest_sigtramp;
			} else if(s>0 && s<32 && (h=guest_sigh[s])>1){
				int ps = (FC) | (FV<<1) | (FZ<<2) | (FN<<3);
				/* V7 one-shot: reset disposition (except ILL/TRAP, kept) */
				if(s!=4 && s!=5){ guest_sigh[s]=0; hs=sig_g2h(s); if(hs) signal(hs,SIG_DFL); }
				SP=(SP-4)&0xffff; st2((SP+2)&0xffff, ps); st2(SP, PC);
				PC=h;			/* enter the libc tvect trampoline */
			} else if(s>0 && s<32 && guest_sigh[s]==0){
				switch(sig_dfl_action(s)){
				case 2:	raise(SIGSTOP); break;	/* really stop; CONT resumes */
				case 1:	break;			/* ignore */
				default: halted=1; ecode=128+s;	/* terminated by signal */
				}
				if(halted) break;
			}
		}
		if(watchsp && (SP<0150000 || SP>0177700)){ fprintf(stderr,"apsim: SP=%06o pc=%06o instr=%06o (i=%lld)\n",SP,PC,ifetch(PC),i); halted=1; ecode=126; break; }
		steppc=PC; pcring[pcri=(pcri+1)&15]=PC;
		if(rndpc==-2){ char*e=getenv("APSIM_RNDPC"); rndpc=e?(int)strtol(e,0,8):-1; }
		if(rndpc>=0 && PC==rndpc) fprintf(stderr,"RND %d r=%d s=%06o ret=%06o\n", rndn++, ld2((R[6]+2)&0xffff), ld2(041202), ld2(R[6]));
		if(trace) fprintf(stderr,"pc=%06o sp=%06o r0=%06o instr=%06o\n",PC,SP,R[0],ifetch(PC));
		if((i&0xFFFFFFF)==0xFFFFFFF) fprintf(stderr,"apsim: %lldM pc=%06o sp=%06o\n",i>>20,PC,SP);
		step();
	}
	if(!halted){ fprintf(stderr,"apsim: instruction limit\n"); return 125; }
	return ecode;
}

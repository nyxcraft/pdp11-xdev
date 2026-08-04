#!/usr/bin/env python3
# mkdual.py -- wrap the V7-family syscall stubs into DUAL-convention stubs.
#
# The one universal libc must serve both the V7 syscall convention (V5..2.9BSD:
# inline/indirect `sys' args, V7-era numbers) and the 4BSD convention
# (2.10/2.11BSD: args already on the C stack, 4.x numbers).  Which one a binary
# needs is known only at RUN time -- from __univ, the era id ld stamped and crt0
# recorded -- so each stub dispatches on it:
#
#     _name:
#         cmp   ___univ,$210
#         blt   Ldv7            / < 210: fall through to the pristine V7 body
#         sys   <4.x-number>.   / >= 210: 4BSD -- args are already at 2(sp)
#         bcc   Ldok            / (the x_norm/x_error exit, inlined: our cerror
#         mov   r0,_errno       /  pops the r5 frame the 4BSD path never set up,
#         mov   $-1,r0          /  so it must NOT be used here)
#     Ldok: rts   pc
#     Ldv7:
#         <original V7 stub body, verbatim>
#
# The dispatch sits BEFORE any frame setup, so the 4BSD trap fires with sp still
# at the return address (args at 2(sp)) and the V7 body's `mov r5,-(sp)' frame is
# untouched.  For __univ < 210 the two added instructions are all the cost.
#
# The 4.x numbers come from the target trees' own syscall.h (authoritative), so
# adding a call is a table row there, not a hand edit here.  Calls with no 4BSD
# number, and the ABI-divergent ones handled elsewhere (DEFER), are copied
# through unwrapped -- V7-only, a documented edge under 2.10/2.11.

import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIBC = os.path.dirname(HERE)

# Calls given a bespoke 4BSD body below (multi-entry, or a non-trivial trap
# sequence) rather than the generic wrap.
SPECIAL = {"sbrk", "exit"}
# Calls whose 4BSD support needs more than a number and is done in a later pass:
# stat/fstat/lstat move to the 52-byte struct (era headers); fork/pipe/wait
# return two values / a status.  Copied through as V7-only for now.
DEFER = {"stat", "fstat", "lstat", "fork", "pipe", "wait"}


def load_sysnums():
    """Read tools/sysnums.tsv (name, bsd210, bsd211) -> two {name: number} maps."""
    n210, n211 = {}, {}
    path = os.path.join(HERE, "sysnums.tsv")
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.rstrip("\n").split("\t")
        name = parts[0]
        if len(parts) > 1 and parts[1]:
            n210[name] = int(parts[1])
        if len(parts) > 2 and parts[2]:
            n211[name] = int(parts[2])
    return n210, n211


# First Edition (V1, __univ==1) uses a THIRD convention: the syscall number is
# the trap operand, any fd is in r0, and the remaining args are INLINE words
# after the trap -- which for a C call are run-time values off the stack, so the
# stub patches those inline words before trapping (self-modifying text; the
# 11/20 had no split I&D).  (name: (v1_number, fd_in_r0, n_inline_args)).
V1SPEC = {
    "write": (4, 1, 2), "read": (3, 1, 2), "close": (6, 1, 0),
    "open": (5, 0, 2), "creat": (8, 0, 2), "unlink": (10, 0, 1),
    "lseek": (19, 1, 2), "chdir": (12, 0, 1),
}


def v1_body(num, fd_in_r0, ninl):
    """The __univ==1 First Edition inline-arg path for one stub."""
    out = ["\tcmp\t___univ,$2", "\tbge\tLnv1"]     # >=2: not V1, use dual head
    if fd_in_r0:
        out.append("\tmov\t2(sp),r0")              # fd
    base = 4 if fd_in_r0 else 2                     # first inline arg's stack slot
    for i in range(ninl):
        slot = "Lv1a" if i == 0 else "Lv1a+%d" % (2 * i)
        out.append("\tmov\t%d(sp),%s" % (base + 2 * i, slot))
    out.append("\tsys\t%d" % num)
    if ninl:
        out.append("Lv1a:\t" + "; ".join(["0"] * ninl) + "\t/ inline args (patched above)")
    out += ["\tbcc\tLv1k", "\tmov\t$-1,r0", "Lv1k:\trts\tpc", "Lnv1:"]
    return out


def norm_body(n210, n211):
    """The 4BSD dispatch head for an ordinary call (norm exit)."""
    out = ["\tcmp\t___univ,$210", "\tblt\tLdv7"]
    if n210 is not None and n211 is not None and n210 != n211:
        out += ["\tcmp\t___univ,$211", "\tblt\tLd210",
                "\tsys\t%d." % n211, "\tbr\tLdchk",
                "Ld210:\tsys\t%d." % n210]
    else:
        n = n211 if n211 is not None else n210
        out += ["\tsys\t%d." % n]
    out += ["Ldchk:\tbcc\tLdok", "\tmov\tr0,_errno", "\tmov\t$-1,r0",
            "Ldok:\trts\tpc", "Ldv7:"]
    return out


def wrap(name, src_lines, n210, n211):
    """Insert the 4BSD dispatch after the stub's entry label."""
    out, done = [], False
    for line in src_lines:
        out.append(line)
        if not done and re.match(r"^_\w+:\s*$", line):
            out.append("\t.globl\t___univ")
            out.append("\t.comm\t_errno,2")
            if name in V1SPEC:
                out += v1_body(*V1SPEC[name])      # __univ==1 First Edition path
            out += norm_body(n210, n211)
            done = True
    if not done:
        sys.stderr.write("mkdual: %s: no entry label found\n" % name)
    return out


# --- bespoke 4BSD bodies (authentic 2.11 logic) ---------------------------
def special_sbrk(n210, n211):
    n = n211 if n211 is not None else n210
    return ("""\
/ generated by mkdual.py -- dual-convention sbrk/brk
.globl	_sbrk, _brk
.globl	_end, cerror, ___univ
.comm	_errno,2

_sbrk:
	cmp	___univ,$210
	blt	Lv7sbrk
/ 4BSD sbrk (2.11): args on the stack; track the break in curbrk
	mov	2(sp),r0		/ increment
	beq	1f
	add	curbrk,r0		/ new break address
	mov	r0,-(sp)
	tst	-(sp)			/ simulate return-addr spacing -> addr at 2(sp)
	sys	%d.
	bes	2f
	cmp	(sp)+,(sp)+
1:	mov	curbrk,r0		/ return old break
	add	2(sp),curbrk
	rts	pc
2:	cmp	(sp)+,(sp)+
	mov	r0,_errno
	mov	$-1,r0
	rts	pc
Lv7sbrk:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(r5),0f
	beq	1f
	add	nd,0f
	sys	0; 9f
	bec	1f
	jmp	cerror
1:
	mov	nd,r0
	add	4(r5),nd
	mov	(sp)+,r5
	rts	pc

_brk:
	cmp	___univ,$210
	blt	Lv7brk
/ 4BSD brk (2.11)
	sys	%d.
	bes	2f
	mov	2(sp),curbrk
	rts	pc
2:	mov	r0,_errno
	mov	$-1,r0
	rts	pc
Lv7brk:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(r5),0f
	sys	0; 9f
	bec	1f
	jmp	cerror
1:
	mov	4(r5),nd
	clr	r0
	mov	(sp)+,r5
	rts	pc

.data
9:
	sys	break; 0:..
nd:	_end
.globl	curbrk
curbrk:	_end
.text
""" % (n, n)).splitlines()


def special_exit(n210, n211):
    n = n211 if n211 is not None else (n210 if n210 is not None else 1)
    return ("""\
/ generated by mkdual.py -- dual-convention _exit
.globl	__exit
.globl	___univ

__exit:
	cmp	___univ,$210
	blt	Lv7exit
	sys	%d.			/ 4BSD: status already at 2(sp); NOTREACHED
Lv7exit:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(r5),r0
	sys	exit
	/*NOTREACHED*/
""" % n).splitlines()


SPECIAL_FN = {"sbrk": special_sbrk, "exit": special_exit}


def main():
    if len(sys.argv) < 3:
        sys.stderr.write("usage: mkdual.py OUTDIR name...\n")
        return 2
    outdir = sys.argv[1]
    names = sys.argv[2:]
    n210, n211 = load_sysnums()
    os.makedirs(outdir, exist_ok=True)
    wrapped = plain = 0
    for name in names:
        src = os.path.join(LIBC, "common", "sys", name + ".s")
        with open(src) as f:
            lines = f.read().splitlines()
        a, b = n210.get(name), n211.get(name)
        if name in SPECIAL_FN:
            lines = SPECIAL_FN[name](a, b); wrapped += 1
        elif name in DEFER or (a is None and b is None):
            plain += 1                      # V7-only passthrough
        else:
            lines = wrap(name, lines, a, b); wrapped += 1
        with open(os.path.join(outdir, name + ".s"), "w") as f:
            f.write("\n".join(lines) + "\n")
    sys.stderr.write("mkdual: %d dual, %d V7-only, into %s\n" % (wrapped, plain, outdir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

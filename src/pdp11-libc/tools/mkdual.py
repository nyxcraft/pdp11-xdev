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
#         cmp   ___univ,$210.   / DECIMAL -- `as' reads a bare $210 as octal (=136)
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
# The stat trio: under 2.10/2.11 the kernel writes the 52-byte 4.3-shape struct
# (its "new" stat number, flagged SR_STAT in apsim), but our one universal
# <sys/stat.h> is the 30-byte V7 shape.  The stub bridges: it traps the 4.x
# number into a 52-byte scratch and repacks into the caller's V7-layout buffer.
# name -> 4.x syscall number (same for 2.10 and 2.11; from sysnums.tsv).
STAT4 = {"stat": 38, "fstat": 62, "lstat": 40}
# Copied through as V7-only under 2.10/2.11.  fork(2) and pipe(42) keep their
# numbers across the renumber and their two-value returns already work (apsim's
# case 2/42), so the pristine V7 stub is correct as-is.  (2.11 needs a bespoke
# wait -- see WAIT4 below -- but 2.10's wait is still the V7 call.)
DEFER = {"fork", "pipe"}
# 2.11BSD pl431 renumbered wait to wait4 (syscall 7 = wait4(pid,*status,opts,
# *rusage)): the status is written through the pointer arg, not returned in r1.
# 2.10 kept the V7 wait (7, status in r1), so only __univ==211 takes this path.
WAIT4 = {"wait"}


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
    """The First Edition (v1 AND v2, __univ 1|2) inline-arg path for one stub."""
    out = ["\tcmp\t___univ,$3", "\tbge\tLnv1"]     # >=3: not First Edition, use dual head
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
    out = ["\tcmp\t___univ,$210.", "\tblt\tLdv7"]   # DECIMAL: $210 would be octal
    if n210 is not None and n211 is not None and n210 != n211:
        out += ["\tcmp\t___univ,$211.", "\tblt\tLd210",
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


def stat_head(name, num):
    """4BSD stat/fstat/lstat head: trap the 4.x number (args already at 2(sp),
    so 2(sp)=path/fd, 4(sp)=buf) into a 52-byte scratch with the caller's buf
    slot redirected, then repack the 4.3 shape into the caller's 30-byte V7
    struct.  Bytes 0..21 (dev..atime) are identical; the 4.x layout inserts a
    2-byte spare before st_mtime and before st_ctime, so those two longs shift.
    mov leaves C untouched, so the restore keeps the syscall's error flag. """
    head = [
        "\t.globl\t___univ",
        "\t.comm\t_errno,2",
        "\tcmp\t___univ,$210.",       # DECIMAL -- $210 is octal (=136) in as
        "\tblt\tLv7%s" % name,        # <210: the pristine V7 indirect body
        "\tmov\t4(sp),Lcb%s" % name,  # save caller buf
        "\tmov\t$Lsc%s,4(sp)" % name, # redirect buf -> 52-byte scratch
        "\tsys\t%d." % num,
        "\tmov\tLcb%s,4(sp)" % name,  # restore caller's arg slot (C preserved)
        "\tbcc\tLk%s" % name,
        "\tmov\tr0,_errno",
        "\tmov\t$-1,r0",
        "\trts\tpc",
        "Lk%s:" % name,
        "\tmov\tLcb%s,r1" % name,     # dst: caller's V7-layout struct
        "\tmov\t$Lsc%s,r0" % name,    # src: 4.3-layout scratch
    ]
    head += ["\tmov\t(r0)+,(r1)+"] * 11   # dev..atime (bytes 0..21, unchanged)
    head += [
        "\ttst\t(r0)+",               # skip 4.x spare1
        "\tmov\t(r0)+,(r1)+",         # st_mtime hi
        "\tmov\t(r0)+,(r1)+",         # st_mtime lo
        "\ttst\t(r0)+",               # skip 4.x spare2
        "\tmov\t(r0)+,(r1)+",         # st_ctime hi
        "\tmov\t(r0)+,(r1)+",         # st_ctime lo
        "\tclr\tr0",
        "\trts\tpc",
        "Lv7%s:" % name,
    ]
    return head


def wrap_stat(name, src_lines, num):
    """Insert the 4BSD stat head after the entry label; keep the V7 body; add
    the scratch + saved-buf cells."""
    out, done = [], False
    for line in src_lines:
        out.append(line)
        if not done and re.match(r"^_%s:\s*$" % name, line):
            out += stat_head(name, num)
            done = True
    if not done:
        sys.stderr.write("mkdual: %s: no entry label found\n" % name)
    out += ["", ".data",
            "Lcb%s:\t.=.+2" % name,     # saved caller-buf pointer
            "Lsc%s:\t.=.+52." % name,   # 52-byte 4.3-shape scratch (decimal!)
            ".text"]
    return out


def wait_head():
    """2.11 wait: call wait4(WAIT_ANY, status, 0, 0).  Build the stackargs frame
    (a leading dummy word stands in for the return-addr slot the kernel skips,
    so the four args land at 2(sp)..8(sp)); wait4 writes the status through the
    pointer, so nothing comes back in r1.  <211 (incl 2.10) keeps the V7 wait."""
    return [
        "\t.globl\t___univ",
        "\tcmp\t___univ,$211.",   # DECIMAL -- `as' reads $211 as octal (=137)
        "\tblt\tLv7wait",
        "\tmov\tr5,-(sp)",
        "\tmov\tsp,r5",
        "\tclr\t-(sp)",          # rusage = 0
        "\tclr\t-(sp)",          # options = 0
        "\tmov\t4(r5),-(sp)",    # *status (the caller's arg)
        "\tmov\t$-1,-(sp)",      # pid = WAIT_ANY
        "\ttst\t-(sp)",          # dummy return-addr slot -> args at 2(sp)..
        "\tsys\t7",              # wait4; r0=pid, *status written through the arg
        "\tmov\tr5,sp",          # pop the frame (mov leaves C = the error flag)
        "\tbcc\tLwok",
        "\tjmp\tcerror",
        "Lwok:",
        "\tmov\t(sp)+,r5",
        "\trts\tpc",
        "Lv7wait:",
    ]


def wrap_wait(src_lines):
    """Insert the 2.11 wait4 head after the entry label; keep the V7 body."""
    out, done = [], False
    for line in src_lines:
        out.append(line)
        if not done and re.match(r"^_wait:\s*$", line):
            out += wait_head()
            done = True
    if not done:
        sys.stderr.write("mkdual: wait: no entry label found\n")
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
	cmp	___univ,$210.
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
	cmp	___univ,$210.
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
	cmp	___univ,$210.
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
        elif name in STAT4:
            lines = wrap_stat(name, lines, STAT4[name]); wrapped += 1
        elif name in WAIT4:
            lines = wrap_wait(lines); wrapped += 1
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

#!/bin/sh
# apsim regression suite.
#
# 1. cistest.s -- the late-hardware golden probe (MFPT, SPL, TSTSET, WRTLCK,
#    FIS, decimal CIS); each numbered case jumps to `fail' with the case
#    number in r5, so a nonzero exit identifies the first failing case.
# 2. errno.s -- errno fidelity: a failing open must deliver the era errno
#    (ENOENT=2), not the historical blanket 1 or a raw host number.
# 3. gate211.s -- universe gate, positive AND negative: sys 64 must be
#    getpagesize under -u bsd211 and must FAIL under the default universe.
#    (The negative half is what stops era numbering leaking across
#    personalities -- the alternative is not a failure but a wrong success.)
# 4. Real era binaries (skipped when the distribution trees are absent):
#    V5/V6 ls|cat from ~/unix/{v5,v6}, and 2.11BSD echo/cat (4.4-style
#    numbering, stack args, string-table a.out) from ~/bsd/2.11.
here=$(cd "$(dirname "$0")" && pwd); BIN="$here/../../../bin"
AS="$BIN/pdp11-as"; APSIM="$BIN/pdp11-apsim"
fail=0; tmp=$(mktemp -d) || exit 1; trap 'rm -rf "$tmp"' EXIT
ok()  { printf '%-11s ok    %s\n' "$1" "$2"; }
bad() { printf '%-11s FAIL  %s\n' "$1" "$2"; fail=1; }
skip(){ printf '%-11s skip  %s\n' "$1" "$2"; }

# ---- 1: late-hardware/CIS instruction golden ---------------------------
if "$AS" -j -o "$tmp/cistest" "$here/cistest.s" && "$APSIM" "$tmp/cistest"; then
	ok cistest 'MFPT/SPL/TSTSET/WRTLCK/FIS/CIS all pass'
else
	bad cistest "first failing case: exit $?"
fi

# ---- 2: errno fidelity -------------------------------------------------
if "$AS" -o "$tmp/errno" "$here/errno.s" && "$APSIM" "$tmp/errno"; then
	ok errno 'failed open delivers era ENOENT in r0'
else
	bad errno "check $? (1=open succeeded, 2=wrong errno)"
fi

# ---- 3: universe numbering gate (positive + negative) ------------------
if "$AS" -o "$tmp/gate211" "$here/gate211.s"; then
	if "$APSIM" -u bsd211 "$tmp/gate211"; then
		if "$APSIM" "$tmp/gate211" 2>/dev/null; then
			bad univ-gate '4.3 numbering honoured under the default universe'
		else
			ok univ-gate 'sys 64 = getpagesize under bsd211 only'
		fi
	else
		bad univ-gate "getpagesize failed under -u bsd211 (exit $?)"
	fi
else
	bad univ-gate 'probe did not assemble'
fi

# ---- 4: real era binaries (need the distribution trees) ----------------
for era in v5 v6; do
	R="$HOME/unix/$era"
	if [ -x "$R/bin/ls" ] && [ -x "$R/bin/cat" ]; then
		out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u $era "$R/bin/ls" / 2>&1)
		case "$out" in
		*bin*etc*|*bin*usr*) ok "$era-ls" 'real 1974/75 ls lists / via the dir snapshot' ;;
		*) bad "$era-ls" "output: $(echo "$out" | head -1)" ;;
		esac
		out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u $era "$R/bin/cat" /etc/passwd 2>&1)
		case "$out" in
		root*) ok "$era-cat" 'cat reads the era /etc/passwd' ;;
		*) bad "$era-cat" "output: $(echo "$out" | head -1)" ;;
		esac
	else
		skip "$era" "~/unix/$era not present"
	fi
done

R="$HOME/bsd/2.11/root"
if [ -x "$R/bin/echo" ] && [ -x "$R/bin/cat" ]; then
	out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/echo" hello 2>&1)
	if [ "$out" = "hello" ]; then
		ok 211-echo 'real 2.11 (4.4-numbered, stack args) echo'
	else
		bad 211-echo "output: $(echo "$out" | head -1)"
	fi
	out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/cat" /etc/motd 2>&1)
	case "$out" in
	*2.11\ BSD*) ok 211-cat 'cat prints the genuine 2.11 motd' ;;
	*) bad 211-cat "output: $(echo "$out" | head -1)" ;;
	esac
	# ls exercises the whole modern stack at once: the 4.3-dirent snapshot,
	# the 52-byte stat shape, tty-gated ioctls (pipe output => single
	# column), qsort, and -- the regression this test pins -- the FPU
	# long-convert operand width (2.11's ldiv is stcfl-based; the old
	# 2-byte step made rts jump to address 0 and restart crt0).
	out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/ls" / 2>&1)
	case "$out" in
	*bin*etc*usr*) ok 211-ls 'real 2.11 ls: sorted listing via dirents + FPU ldiv' ;;
	*) bad 211-ls "output: $(echo "$out" | head -1)" ;;
	esac
	out=$(APSIM_TIME=200000000 APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/date" 2>&1)
	case "$out" in
	*1976*) ok 211-date 'date renders the pinned clock through the tz engine' ;;
	*) bad 211-date "output: $(echo "$out" | head -1)" ;;
	esac
	# Script exec: "#!/bin/echo hi" must exec echo as [hi, scriptpath]
	# (the classic one-interpreter one-optional-arg kernel rule), and the
	# shebang-less text /bin/true ("exit 0") must run via the guest
	# /bin/sh -- the command-line courtesy; a guest exec still gets the
	# authentic ENOEXEC so shells do their own fallback.
	printf '#!/bin/echo hi\n' > "$tmp/sb"
	out=$(APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$tmp/sb" 2>&1)
	case "$out" in
	hi\ *) ok 211-shebang '#! line execs the interpreter, classic argv' ;;
	*) bad 211-shebang "output: $(echo "$out" | head -1)" ;;
	esac
	if APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/true" >/dev/null 2>&1 && \
	   ! APSIM_ROOT="$R" timeout 10 "$APSIM" -u bsd211 "$R/bin/false" >/dev/null 2>&1; then
		ok 211-script 'shebang-less text runs via guest /bin/sh (true=0 false=1)'
	else
		bad 211-script 'true/false exit statuses wrong'
	fi
	# Job control: the genuine csh (a 0431 separate-I&D overlay binary)
	# must fork external commands, reap them through the 4.3 SIGCHLD
	# frame (sigvec/sigtramp/sigreturn), run a foreach loop of externals,
	# and report background jobs "Done".  This is the whole job-control
	# path -- overlays, 32-bit sigmasks, sigsuspend, wait3 -- end to end.
	if [ -x "$R/bin/csh" ]; then
		out=$(printf 'foreach i (a b c)\n/bin/echo item $i\nend\nexit\n' | \
		      APSIM_ROOT="$R" timeout 15 "$APSIM" -u bsd211 "$R/bin/csh" -f 2>&1)
		nl=$(printf '%s\n' "$out" | grep -c '^item ')
		if [ "$nl" = 3 ]; then
			ok 211-csh 'csh forks/reaps externals: foreach loop of 3'
		else
			bad 211-csh "csh job control: got [$out]"
		fi
		out=$(printf '/bin/echo J &\nwait\nexit\n' | \
		      APSIM_ROOT="$R" timeout 15 "$APSIM" -u bsd211 "$R/bin/csh" -f 2>&1)
		case "$out" in
		*Done*) ok 211-csh-bg 'csh background job reaped and reported Done' ;;
		*) bad 211-csh-bg "no Done report: [$out]" ;;
		esac
	else
		skip 211-csh 'csh not present'
	fi
	# Sockets: socketpair(AF_UNIX) end to end -- socket()/send/recv on real
	# host sockets, exercising the guest fd = host fd model and the 2.11
	# stack-arg convention.  Self-contained (no network config).
	if "$AS" -o "$tmp/sockpair" "$here/sockpair.s"; then
		out=$(APSIM_ROOT=/ timeout 10 "$APSIM" -u bsd211 "$tmp/sockpair" 2>&1)
		[ "$out" = "hi!" ] && ok 211-socket 'socketpair send/recv over real host sockets' \
		                   || bad 211-socket "output: [$out]"
	else
		bad 211-socket 'sockpair.s did not assemble'
	fi
else
	skip bsd211 '~/bsd/2.11/root not present'
fi

[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES"; exit $fail

# libc: exercise the C library beyond printf -- the string/memory, numeric,
# qsort, and buffered file-I/O routines imported from the authentic 2.8BSD
# libc.  Each program is compiled with our cc, linked against usr/lib/libc.a,
# and run in apsim; we check its stdout (and a written file) end to end.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# --- string / memory / numeric / qsort ------------------------------------
cat > "$tmp/s.c" <<'EOF'
int icmp(a,b) char *a,*b; { return *(int*)a - *(int*)b; }
int main(){
	char buf[40]; int v[5]; int i;
	strcpy(buf, "hello");
	strcat(buf, ", world");
	printf("%d %d %d\n", strlen(buf), strcmp(buf,"hello, world"), index(buf,'w')-buf);
	printf("%d %d\n", atoi("  -123xy"), abs(-7));
	v[0]=5; v[1]=2; v[2]=8; v[3]=1; v[4]=9;
	qsort(v, 5, sizeof(int), icmp);
	for(i=0;i<5;i++) printf("%d", v[i]);
	printf("\n");
	return 0;
}
EOF
( cd "$tmp" && "$BIN-cc" s.c -o s ) || fail "string/qsort: cc failed"
out=`"$SIM" "$tmp/s" 2>/dev/null`
check_eq "string/numeric/qsort" "12 0 7
-123 7
12589" "$out"

# --- long arithmetic (pulls in lmul/ldiv/lrem from crt/) -------------------
cat > "$tmp/l.c" <<'EOF'
int main(){ long a; a = 1000000L; a = a * 1000L; printf("%ld\n", a / 7L); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" l.c -o l ) || fail "long: cc failed"
check_eq "long mul/div" "142857142" "`"$SIM" "$tmp/l" 2>/dev/null`"

# --- buffered file I/O: fopen/fputs/fprintf/fclose then fopen/fgets --------
cat > "$tmp/f.c" <<'EOF'
#include <stdio.h>
int main(){
	FILE *fp; char line[64]; int n;
	fp = fopen("FNAME", "w");
	if(!fp){ printf("open-w fail\n"); return 1; }
	fputs("alpha\n", fp); fprintf(fp, "beta %d\n", 42);
	fclose(fp);
	fp = fopen("FNAME", "r");
	if(!fp){ printf("open-r fail\n"); return 1; }
	n = 0;
	while(fgets(line, sizeof line, fp)){ printf("%s", line); n++; }
	fclose(fp);
	printf("lines=%d\n", n);
	return 0;
}
EOF
sed "s#FNAME#$tmp/data.txt#g" "$tmp/f.c" > "$tmp/f2.c"
( cd "$tmp" && "$BIN-cc" f2.c -o f ) || fail "fileio: cc failed"
check_eq "file I/O round-trip" "alpha
beta 42
lines=2" "`"$SIM" "$tmp/f" 2>/dev/null`"

# --- malloc / free / realloc (authentic 2.8BSD free-list allocator) -------
cat > "$tmp/m.c" <<'EOF'
char *malloc(), *realloc();
int main(){
	char *a, *b; int i, bad;
	a = malloc(100);
	for (i = 0; i < 99; i++) a[i] = 'A' + (i % 26);
	a[99] = 0;
	b = malloc(50); strcpy(b, "block-b");
	a = realloc(a, 300);          /* move + preserve across an intervening block */
	bad = 0;
	for (i = 0; i < 99; i++) if (a[i] != 'A' + (i % 26)) bad++;
	printf("realloc preserved=%d b=[%s]\n", bad == 0, b);
	free(a); free(b);
	a = malloc(40); strcpy(a, "reused");   /* reuse freed space */
	printf("reuse=[%s]\n", a);
	return 0;
}
EOF
( cd "$tmp" && "$BIN-cc" m.c -o m ) || fail "malloc: cc failed"
check_eq "malloc/free/realloc" "realloc preserved=1 b=[block-b]
reuse=[reused]" "`"$SIM" "$tmp/m" 2>/dev/null`"

cat > "$tmp/ca.c" <<'EOF'
char *calloc();
int main(){ int *a, i, nz; a = (int *)calloc(20, sizeof(int));
	nz = 0; for (i = 0; i < 20; i++) if (a[i] != 0) nz++;
	printf("calloc nonzero=%d\n", nz); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" ca.c -o ca ) || fail "calloc: cc failed"
check_eq "calloc zeroes" "calloc nonzero=0" "`"$SIM" "$tmp/ca" 2>/dev/null`"

# --- sprintf (varargs -> _doprnt into a string) ---------------------------
cat > "$tmp/sp.c" <<'EOF'
int main(){ char b[80]; sprintf(b, "%d/%s/%x/%o/%d", 42, "hi", 255, 64, -5);
	printf("%s\n", b); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" sp.c -o sp ) || fail "sprintf: cc failed"
check_eq "sprintf" "42/hi/ff/100/-5" "`"$SIM" "$tmp/sp" 2>/dev/null`"

# --- getchar/putchar through a pipe ---------------------------------------
cat > "$tmp/c.c" <<'EOF'
int main(){ int c; while((c=getchar())>=0) putchar(c); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" c.c -o c ) || fail "getchar: cc failed"
check_eq "getchar/putchar" "PDP-11/2.8BSD" "`printf 'PDP-11/2.8BSD' | "$SIM" "$tmp/c" 2>/dev/null`"

exit 0

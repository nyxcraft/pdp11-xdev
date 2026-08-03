#!/usr/bin/env python3
"""
rogue_driver.py -- drive 2.8BSD rogue running under apsim through a PTY.

apsim's terminal emulation (gtty/stty/raw mode) needs a real tty; the PTY this
module allocates provides one, so rogue runs interactively under program
control.  A small ANSI parser renders rogue's cursor-addressed output into a
24x80 screen grid we can read (find the @, parse the status line, see the map).

The seed is pinned via APSIM_PID (rogue: seed = getpid(), a pure LCG), so a game
is fully reproducible -- which lets us run the SAME seed + SAME keystrokes
through the original rogue3.4.obj and our toolchain-built rogue and diff the
screens step for step (RogueDiffer below).  Any divergence is a behavioral
difference between the binaries that static byte-matching might miss.

Usage:
    from rogue_driver import Rogue, RogueDiffer
    r = Rogue(seed=12345); r.start(); print(r.screen_text()); r.send('l'); ...
    RogueDiffer(seed=12345).run(commands="hjkl" * 5)   # compare orig vs ours
"""
import os, pty, select, time, signal, sys

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APSIM = os.path.join(os.path.dirname(TOP), "bin", "pdp11-apsim")
ORIG_ROGUE = os.path.join(os.path.expanduser("~"), "rogue3.4", "rogue3.4.obj")
ROOT = os.path.expanduser(os.environ.get("APSIM_ROOT", "~/native-as/root"))

ROWS, COLS = 24, 80


def _ensure_root(root):
    """Create the faux 2.8 root rogue needs: /etc/passwd + /etc/termcap."""
    os.makedirs(os.path.join(root, "etc"), exist_ok=True)
    os.makedirs(os.path.join(root, "tmp"), exist_ok=True)
    os.makedirs(os.path.join(root, "usr", "games"), exist_ok=True)
    pw = os.path.join(root, "etc", "passwd")
    if not os.path.exists(pw):
        open(pw, "w").write("root:*:0:0:Operator:/:/bin/sh\n"
                            "rogue:*:1:1:Rogue Player:/usr/games:/bin/sh\n")
    tc = os.path.join(root, "etc", "termcap")
    if not os.path.exists(tc):
        open(tc, "w").write(
            "va|ansi|vt100|ansi/vt100 terminal:\\\n"
            "\t:co#80:li#24:am:bs:\\\n"
            "\t:cm=\\E[%i%d;%dH:cl=\\E[H\\E[J:ce=\\E[K:cd=\\E[J:\\\n"
            "\t:up=\\E[A:nd=\\E[C:ho=\\E[H:so=\\E[7m:se=\\E[m:us=\\E[4m:ue=\\E[m:\n")


class Screen:
    """A 24x80 grid updated by a minimal ANSI/vt100 parser (the subset rogue's
    termcap entry above emits: absolute cursor address, clear, erase-to-eol/eos,
    plus \\b \\r \\n \\t)."""
    def __init__(self):
        self.grid = [[" "] * COLS for _ in range(ROWS)]
        self.r = self.c = 0

    def feed(self, data):
        i, n = 0, len(data)
        while i < n:
            ch = data[i]
            if ch == 0x1b and i + 1 < n and data[i + 1] == ord('['):
                j = i + 2
                while j < n and not (0x40 <= data[j] <= 0x7e):
                    j += 1
                if j < n:
                    self._csi(chr(data[j]), data[i + 2:j].decode("latin1"))
                    i = j + 1
                    continue
                break
            elif ch == ord('\r'):
                self.c = 0
            elif ch == ord('\n'):
                self.r = min(self.r + 1, ROWS - 1)
            elif ch == ord('\b'):
                self.c = max(self.c - 1, 0)
            elif ch == ord('\t'):
                self.c = min((self.c + 8) & ~7, COLS - 1)
            elif ch == 7:       # BEL
                pass
            elif 32 <= ch < 127:
                if self.c < COLS:
                    self.grid[self.r][self.c] = chr(ch)
                    self.c += 1
            i += 1

    def _csi(self, final, params):
        nums = [int(x) for x in params.split(';') if x.isdigit()]
        if final == 'H' or final == 'f':           # cursor position (1-based)
            self.r = (nums[0] - 1) if len(nums) > 0 else 0
            self.c = (nums[1] - 1) if len(nums) > 1 else 0
            self.r = max(0, min(self.r, ROWS - 1)); self.c = max(0, min(self.c, COLS - 1))
        elif final == 'J':                          # erase display
            mode = nums[0] if nums else 0
            if mode == 2 or mode == 0:
                start = 0 if mode == 2 else self.r
                for rr in range(start, ROWS):
                    c0 = 0 if (mode == 2 or rr > self.r) else self.c
                    for cc in range(c0, COLS): self.grid[rr][cc] = " "
        elif final == 'K':                          # erase to end of line
            for cc in range(self.c, COLS): self.grid[self.r][cc] = " "
        elif final == 'A': self.r = max(0, self.r - (nums[0] if nums else 1))
        elif final == 'B': self.r = min(ROWS - 1, self.r + (nums[0] if nums else 1))
        elif final == 'C': self.c = min(COLS - 1, self.c + (nums[0] if nums else 1))
        elif final == 'D': self.c = max(0, self.c - (nums[0] if nums else 1))
        # 'm' (SGR/colour) and others: ignored -- rogue's display is monochrome

    def text(self):
        return "\n".join("".join(row).rstrip() for row in self.grid)

    def line(self, r):
        return "".join(self.grid[r]).rstrip()

    def at(self, r, c):
        if 0 <= r < ROWS and 0 <= c < COLS:
            return self.grid[r][c]
        return " "

    def find_all(self, chars):
        out = []
        for r in range(ROWS):
            for c in range(COLS):
                if self.grid[r][c] in chars:
                    out.append((r, c, self.grid[r][c]))
        return out

    def find(self, ch):
        for r in range(ROWS):
            for c in range(COLS):
                if self.grid[r][c] == ch:
                    return (r, c)
        return None


class Rogue:
    def __init__(self, binary=ORIG_ROGUE, seed=12345, root=ROOT, term="ansi"):
        self.binary, self.seed, self.root, self.term = binary, seed, root, term
        self.pid = self.fd = None
        self.screen = Screen()

    def start(self):
        _ensure_root(self.root)
        pid, fd = pty.fork()
        if pid == 0:                       # child: become apsim under the pty
            env = {
                "APSIM_ROOT": self.root,
                "APSIM_ENV": "TERM=%s HOME=/usr/games ROGUEOPTS=" % self.term,
                "PATH": "/usr/bin:/bin",
            }
            # -p pins getpid() -> rogue's seed, for reproducible games
            os.execve(APSIM, [APSIM, "-p", str(self.seed), self.binary], env)
            os._exit(127)
        self.pid, self.fd = pid, fd
        self._drain(settle=1.2)            # let it dig the dungeon + draw
        return self

    def _drain(self, settle=0.4, hard=8.0):
        """Read until no output for `settle` seconds (screen has stopped
        changing), or `hard` seconds total."""
        end = time.time() + hard
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], settle)
            if not r:
                break
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                break
            if not data:
                break
            self.screen.feed(data)
        return self

    def send(self, keys, settle=0.4):
        if isinstance(keys, str):
            keys = keys.encode("latin1")
        os.write(self.fd, keys)
        self._drain(settle=settle)
        return self

    def screen_text(self):
        return self.screen.text()

    def status(self):
        """Parse rogue's bottom status line: Level/Gold/Hp/Str/Ac/Exp."""
        import re
        for r in (ROWS - 1, ROWS - 2):
            ln = self.screen.line(r)
            m = re.search(r"Level:\s*(\d+).*Gold:\s*(\d+).*Hp:\s*(\d+)\((\d+)\)"
                          r".*Str:\s*(\d+).*Ac:\s*(-?\d+).*Exp:\s*(\d+)/(\d+)", ln)
            if m:
                k = ["level", "gold", "hp", "maxhp", "str", "ac", "explevel", "exp"]
                return dict(zip(k, map(int, m.groups())))
        return None

    def player(self):
        return self.screen.find("@")

    def alive(self):
        t = self.screen.text().lower()
        return "killed by" not in t and "rip" not in t and self.player() is not None

    def close(self):
        if self.pid:
            try:
                os.write(self.fd, b"Qy")       # try a clean quit
                time.sleep(0.2)
            except OSError:
                pass
            try:
                os.kill(self.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(self.pid, 0)
            except ChildProcessError:
                pass
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.pid = self.fd = None


class RogueDiffer:
    """Run the same seed + same keystrokes through two rogue binaries and
    compare the rendered screens after each command.  Default: original
    rogue3.4.obj vs our toolchain-built rogue."""
    def __init__(self, seed=12345, a=ORIG_ROGUE, b=None):
        self.seed = seed
        self.a = a
        self.b = b      # if None, caller must build & pass our rogue path

    def run(self, commands, per_step=True, settle=0.4):
        ra = Rogue(self.a, self.seed).start()
        rb = Rogue(self.b, self.seed).start()
        diffs = []
        try:
            if ra.screen_text() != rb.screen_text():
                diffs.append(("start", ra.screen_text(), rb.screen_text()))
            for i, key in enumerate(commands):
                ra.send(key, settle); rb.send(key, settle)
                if per_step and ra.screen_text() != rb.screen_text():
                    diffs.append((i, key, ra.screen_text(), rb.screen_text()))
        finally:
            ra.close(); rb.close()
        return diffs


if __name__ == "__main__":
    # smoke test: start the original rogue, show the opening screen + status
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 12345
    r = Rogue(seed=seed).start()
    print("=== seed %d, opening screen ===" % seed)
    print(r.screen_text())
    print("=== status:", r.status())
    print("=== player @:", r.player())
    r.close()

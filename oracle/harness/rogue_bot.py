#!/usr/bin/env python3
"""
rogue_bot.py -- a bot that plays 2.8BSD rogue running under apsim.

Built on rogue_driver.py (PTY + ANSI screen model).  It starts with real rogue
3.4 game knowledge (monster danger table + hazards from monsters.c/init.c, item
symbols, potion/scroll categories), parses the on-screen map, pathfinds with
BFS, and follows a simple policy: fight weak monsters, flee deadly ones (never
melee a floating eye -- it paralyzes), grab items, explore, and descend.

    python3 sim/rogue_bot.py [seed] [max_turns]
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rogue_driver import Rogue, ORIG_ROGUE, ROWS, COLS

# ---- baked-in game knowledge (from rogue3.4 init.c / monsters.c) ----
# letter -> (name, exp-value danger proxy, hazard note)
MONSTERS = {
    'A': ("giant ant", 10, "drains strength"),  'B': ("bat", 3, ""),
    'C': ("centaur", 10, ""),                    'D': ("dragon", 9000, "DEADLY breath"),
    'E': ("floating eye", 5, "PARALYZES-no melee"), 'F': ("violet fungi", 85, "holds you"),
    'G': ("gnome", 8, ""),                       'H': ("hobgoblin", 3, ""),
    'I': ("invisible stalker", 120, "strong"),   'J': ("jackal", 2, ""),
    'K': ("kobold", 1, ""),                      'L': ("leprechaun", 10, "steals gold"),
    'M': ("mimic", 140, "disguised item"),       'N': ("nymph", 40, "steals item"),
    'O': ("orc", 5, ""),                         'P': ("purple worm", 7000, "DEADLY"),
    'Q': ("quasit", 35, ""),                     'R': ("rust monster", 20, "rusts armor"),
    'S': ("snake", 3, ""),                       'T': ("troll", 55, "regenerates"),
    'U': ("umber hulk", 130, "strong"),          'V': ("vampire", 380, "drains maxHP"),
    'W': ("wraith", 55, "drains exp"),           'X': ("xorn", 120, "strong"),
    'Y': ("yeti", 50, ""),                       'Z': ("zombie", 7, ""),
}
NO_MELEE = set("E")                 # floating eye: paralysis -> death; never melee
ITEMS = "!?:)]=/,*"                 # potion scroll food weapon armor ring stick amulet gold
ITEM_NAME = {'!': "potion", '?': "scroll", ':': "food", ')': "weapon", ']': "armor",
             '=': "ring", '/': "wand/staff", ',': "amulet", '*': "gold"}
WALK = set(".#+%") | set(ITEMS)     # cells the player can step onto
# potion/scroll categories the bot "knows" exist (random-named per game until IDed)
GOOD_POTIONS = {"healing", "extra healing", "gain strength", "restore strength", "raise level"}
BAD_POTIONS = {"poison", "confusion", "paralysis", "blindness"}
GOOD_SCROLLS = {"identify", "enchant a weapon", "gain armor", "remove curse", "magic mapping"}
BAD_SCROLLS = {"aggravate monsters", "create a monster"}

# movement keys -> (dr, dc), including diagonals
MOVES = [('h', 0, -1), ('l', 0, 1), ('k', -1, 0), ('j', 1, 0),
         ('y', -1, -1), ('u', -1, 1), ('b', 1, -1), ('n', 1, 1)]
DELTA = {k: (dr, dc) for k, dr, dc in MOVES}


class RogueBot:
    def __init__(self, rogue, verbose=True):
        self.r = rogue
        self.v = verbose
        self.stairs = None      # remembered stairs location (@ hides the % we stand on)
        self.last_pos = None
        self.stuck = 0
        self.turns = 0
        self.log = []
        self.rock = set()       # cells a move bounced off (unknown ' ' that was solid)
        self.prev_p = None      # @ position before the last move
        self.prev_target = None # cell the last move tried to enter
        self.searches = 0       # consecutive searches (for hidden passages)
        self.goal = None        # committed exploration target (avoids oscillation)
        self.goal_age = 0

    # ---- perception ----
    def grid(self): return self.r.screen.grid
    def at(self, r, c): return self.r.screen.at(r, c)

    def player(self): return self.r.player()

    def monsters(self):
        out = []
        for r in range(1, ROWS - 1):
            for c in range(COLS):
                ch = self.grid()[r][c]
                if 'A' <= ch <= 'Z':
                    out.append((r, c, ch))
        return out

    def items(self):
        # only the map area (row 0 is messages, row 23 the status line, which
        # contain ':' '/' ')' etc. that are NOT map items)
        return [(r, c, ch) for r, c, ch in self.r.screen.find_all(ITEMS)
                if 1 <= r <= ROWS - 2]

    # ---- navigation ----
    def walkable(self, r, c):
        return self.at(r, c) in WALK

    def bfs(self, start, goals, avoid=()):
        """First-step move key from start toward the nearest goal cell.
        Traverses known walkable cells, and -- crucially for exploration --
        steps into unknown (' ') cells from a door/passage/corridor, since
        that's how rogue corridors are revealed.  Diagonal moves into/out of a
        doorway are disallowed (rogue forbids them)."""
        if not goals:
            return None
        from collections import deque
        q = deque([start]); came = {start: None}
        avoid = set(avoid)
        while q:
            cur = q.popleft()
            if cur in goals and cur != start:
                step = cur
                while came[step] != start:
                    step = came[step]
                dr, dc = step[0] - start[0], step[1] - start[1]
                for k, ddr, ddc in MOVES:
                    if (ddr, ddc) == (dr, dc):
                        return k
                return None
            r, c = cur
            frm = self.at(r, c)
            for _, dr, dc in MOVES:
                nr, nc = r + dr, c + dc
                np = (nr, nc)
                if np in came or np in avoid:
                    continue
                if not (0 <= nr < ROWS and 0 <= nc < COLS):
                    continue
                to = self.at(nr, nc)
                diag = dr != 0 and dc != 0
                if diag and ('+' in (frm, to)):     # no diagonal through doors
                    continue
                ok = to in WALK
                if not ok and to == ' ' and frm in "#+ " and not diag:
                    ok = True                       # thread out a door / along a corridor
                if np in goals:
                    ok = True
                if ok:
                    came[np] = cur
                    q.append(np)
        return None

    def frontier(self):
        """The exploration edge: the UNKNOWN (' ') cells we can walk into next
        -- i.e. unknown cells orthogonally adjacent to a door or passage (a
        corridor we haven't followed yet).  Returning only the unknown targets
        (not the known floor/door cells beside them) avoids ping-ponging
        between two adjacent already-explored frontier cells."""
        goals = set()
        for r in range(1, ROWS - 1):
            for c in range(1, COLS - 1):
                if self.at(r, c) != ' ':
                    continue
                # orthogonally adjacent to a passage/door = a corridor entrance
                for dr, dc in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                    if self.at(r + dr, c + dc) in "#+":
                        goals.add((r, c)); break
        return goals

    # ---- messages ----
    def clear_more(self):
        for _ in range(6):
            top = self.r.screen.line(0)
            if '--More--' in top or '-more-' in top.lower():
                self.r.send(' ', settle=0.15)
            else:
                break

    def message(self):
        return self.r.screen.line(0)

    # ---- the policy ----
    def step(self):
        self.clear_more()
        if not self.r.alive():
            return False
        p = self.player()
        if p is None:
            self.r.send(' ', settle=0.2)         # maybe a prompt; nudge
            return self.r.alive()
        pr, pc = p
        mons = self.monsters()
        # learn impassable cells: if the last move didn't change @, the cell we
        # tried to enter was solid (unknown ' ' that turned out to be rock).
        if self.prev_p is not None and p == self.prev_p and self.prev_target:
            self.rock.add(self.prev_target)
        # track stuck-ness
        if p == self.last_pos: self.stuck += 1
        else: self.stuck = 0
        self.last_pos = p
        st = self.r.status() or {}
        hp, maxhp = st.get('hp', 12), st.get('maxhp', 12)

        # 1. adjacent monster handling
        adj = [(r, c, ch) for r, c, ch in mons if abs(r - pr) <= 1 and abs(c - pc) <= 1]
        for r, c, ch in adj:
            if ch in NO_MELEE:
                key = self._away(pr, pc, r, c)      # flee paralysing eye
                if key: return self._do(key, "flee %s (%s)" % (MONSTERS[ch][0], MONSTERS[ch][2]))
            danger = MONSTERS.get(ch, ('?', 0, ''))[1]
            if hp * 3 < maxhp and danger > 30:
                key = self._away(pr, pc, r, c)
                if key: return self._do(key, "flee %s (low HP %d/%d)" % (MONSTERS[ch][0], hp, maxhp))
            # otherwise attack: move into it
            key = self._toward(pr, pc, r, c)
            if key: return self._do(key, "attack %s" % MONSTERS[ch][0])

        # 2. grab a reachable item (skip if a deadly monster is very close)
        threat = any(MONSTERS.get(ch, ('', 0, ''))[1] > 50 and abs(r-pr)+abs(c-pc) <= 4
                     for r, c, ch in mons)
        if not threat:
            its = self.items()
            if its:
                key = self.bfs((pr, pc), {(r, c) for r, c, _ in its})
                if key:
                    tgt = min(its, key=lambda t: abs(t[0]-pr)+abs(t[1]-pc))
                    return self._do(key, "fetch %s" % ITEM_NAME.get(tgt[2], tgt[2]))

        # 3. remember stairs; descend if standing on them and area looks explored
        st_cells = {(r, c) for r, c, ch in self.r.screen.find_all('%')}
        if st_cells: self.stairs = min(st_cells, key=lambda t: abs(t[0]-pr)+abs(t[1]-pc))
        if self.stairs == (pr, pc) or self.at(pr, pc) == '%':
            return self._do('>', "descend stairs")

        # 4. explore toward the frontier, COMMITTING to one target so we don't
        #    oscillate between two equidistant frontiers each turn.
        front = self.frontier() - self.rock
        # keep the current goal if it's still a frontier and we're making progress
        if (self.goal not in front) or self.goal in self.rock or self.goal_age > 40:
            self.goal = None
        if self.goal is None and front:
            self.goal = min(front, key=lambda f: abs(f[0]-pr) + abs(f[1]-pc))
            self.goal_age = 0
        if self.goal:
            key = self.bfs((pr, pc), {self.goal}, avoid=self.rock)
            if key:
                self.searches = 0; self.goal_age += 1
                return self._do(key, "explore")
            self.goal = None        # unreachable -> drop it

        # 5. nothing reachable to explore: search for hidden passages a few times
        #    (rogue hides some doors/corridors), then fall back to the stairs.
        if self.searches < 8:
            self.searches += 1
            return self._do('s', "search")

        # 6. go to / take the stairs to the next level
        if self.stairs:
            key = self.bfs((pr, pc), {self.stairs}, avoid=self.rock)
            if key: return self._do(key, "go to stairs")
        return self._do(self._random_walk(pr, pc), "wander")

    def _toward(self, pr, pc, r, c):
        dr = (r > pr) - (r < pr); dc = (c > pc) - (c < pc)
        for k, ddr, ddc in MOVES:
            if (ddr, ddc) == (dr, dc): return k
        return None

    def _away(self, pr, pc, r, c):
        dr = (pr > r) - (pr < r); dc = (pc > c) - (pc < c)
        # pick a walkable retreat
        for k, ddr, ddc in MOVES:
            if (ddr, ddc) == (dr, dc) and self.walkable(pr+ddr, pc+ddc): return k
        for k, ddr, ddc in MOVES:
            if self.walkable(pr+ddr, pc+ddc) and (pr+ddr, pc+ddc) != (r, c): return k
        return None

    def _random_walk(self, pr, pc):
        for k, dr, dc in MOVES:
            if self.walkable(pr+dr, pc+dc): return k
        return 's'

    def _do(self, key, why):
        self.turns += 1
        self.log.append((self.turns, why, self.message().strip()))
        if self.v:
            st = self.r.status() or {}
            print("[%3d] %-22s Lvl %s HP %s/%s  %s" % (
                self.turns, why, st.get('level','?'),
                st.get('hp','?'), st.get('maxhp','?'), self.message().strip()[:40]))
        # remember where this move intends to land, so a bounced move (didn't
        # change @) can mark that cell solid next turn
        p = self.player()
        if p and key in DELTA:
            dr, dc = DELTA[key]
            self.prev_p = p
            self.prev_target = (p[0] + dr, p[1] + dc)
        else:
            self.prev_p = None; self.prev_target = None
        self.r.send(key, settle=0.22)
        return True

    def play(self, max_turns=400, show_every=0):
        while self.turns < max_turns:
            if show_every and self.turns % show_every == 0:
                print(self.r.screen_text())
            if not self.step():
                break
        st = self.r.status() or {}
        alive = self.r.alive()
        print("\n=== game over: %s after %d turns, level %s, HP %s, gold %s ==="
              % ("ALIVE" if alive else "DIED", self.turns,
                 st.get('level','?'), st.get('hp','?'), st.get('gold','?')))
        if not alive:
            print("\n".join(self.r.screen_text().split("\n")[:16]))
        return st


if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 12345
    maxt = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    binary = ORIG_ROGUE
    r = Rogue(binary, seed).start()
    print("=== rogue bot, seed %d ===" % seed)
    print(r.screen_text())
    bot = RogueBot(r)
    bot.play(max_turns=maxt)
    r.close()

#!/usr/bin/env python3
"""Convert the "Christmas Carols" Apple II disk into data for a Griffin BASIC player.

usage: tools/carols.py DISKDIR                 summary of every carol
       tools/carols.py DISKDIR --trace N       the events of carol N, one per line
       tools/carols.py DISKDIR --data FILE     DATA statements for every carol,
                                               as lines 9000 and up of a Griffin
                                               BASIC program

DISKDIR is the unpacked disk: the directory holding A1.bas ... A38.bas, which
are listings of Integer BASIC programs.  Each plays one carol by calling a
machine-language routine per note (CALL address, with the length POKEd into
2057 first) and printing the words between phrases.

Nothing here rewrites those programs.  Each one is run by the small
interpreter below, which covers just the statements they use, and what it
would have done is recorded as a list of events:

    ("note", semitone, units)   semitone is a MIDI note number (60 = middle C)
    ("rest", ms)                an empty FOR loop
    ("text", row, col, words)   row 1-24 and col 1-40, as on the Apple's screen
    ("clear",)                  CALL -936, which every carol does between verses

In the DATA each event is one number, sometimes with more after it:

    pitch * 100 + units         a note (so 100 or more)
    -ms                         a rest
    0, row, col, "words"        text
    1                           clear the screen
    2                           the end of the carol
    3                           this carol is not on the disk

Line 9000 holds the number of carols and 9001 on their titles.  GOSUB 9500
with `carol` set does the RESTORE for that carol's events.

Measured from the machine code (see STATUS.md for how far that is trusted):
a note lasts (length - 1) units of about 88 ms, whatever its pitch.
"""
import os
import re
import sys

UNIT_MS = 88                # one unit of note length
LOOP_MS = 1.0               # one turn of an empty Integer BASIC FOR loop: a guess
LENGTH_ADDRESS = 2057       # where the note routines read the length
HOME = -936                 # Monitor: clear the screen

# Entry point of each note routine, as a MIDI note number.  The routines play
# about 45 cents flat of these, all by the same amount.
NOTES = {
    2496: 46, 2608: 47, 2721: 48, 2832: 49, 2944: 50, 4138: 51, 3115: 52, 3200: 53,
    4224: 54, 3373: 55, 3460: 56, 4310: 57, 3610: 58, 3684: 59, 4458: 60, 3840: 61,
    3914: 62, 3988: 63, 4384: 64, 4533: 65, 4608: 66, 4682: 67, 4757: 68,
    4452: 60,               # a typing slip in carol 30 for 4458
}

TITLES = [
    "ANGELS FROM THE REALMS OF GLORY", "ANGELS WE HAVE HEARD ON HIGH", "AWAY IN A MANGER",
    "CAROLING, CAROLING", "THE COVENTRY CAROL", "DECK THE HALLS", "THE FIRST NOEL",
    "FROSTY THE SNOWMAN", "GOD REST YOU MERRY, GENTLEMEN", "GOOD CHRISTIAN MEN, REJOICE",
    "GOOD KING WENCESLAS", "HARK! THE HERALD ANGELS SING",
    "HAVE YOURSELF A MERRY LITTLE XMAS", "HERE COMES SANTA CLAUS",
    "HERE WE COME A-WASSAILING", "THE HOLLY AND THE IVY", "A HOLLY JOLLY CHRISTMAS",
    "HOME FOR THE HOLIDAYS", "I HEARD THE BELLS ON CHRISTMAS DAY",
    "I'LL BE HOME FOR CHRISTMAS", "IT CAME UPON THE MIDNIGHT CLEAR", "JINGLE BELLS",
    "JOLLY OLD SAINT NICOLAS", "JOY TO THE WORLD!", "LET IT SNOW!",
    "THE NIGHT BEFORE CHRISTMAS", "O CHRISTMAS TREE", "O COME ALL YE FAITHFUL",
    "O HOLY NIGHT", "O LITTLE TOWN OF BETHLEHEM", "RUDOLPH THE RED-NOSED REINDEER",
    "SANTA CLAUS IS COMING TO TOWN", "SILENT NIGHT", "THE TWELVE DAYS OF CHRISTMAS",
    "WE THREE KINGS OF ORIENT ARE", "WE WISH YOU A MERRY CHRISTMAS", "WHAT CHILD IS THIS?",
    "WHILE SHEPHERDS WATCHED THEIR FLOCKS",
]                           # as the disk's MENU program lists them


class Problem(Exception):
    pass


# ---- the interpreter ------------------------------------------------------------

def split_statements(text):
    """Statements of one line: split at colons that are outside strings."""
    return [s.strip() for s in re.findall(r'(?:"[^"]*"|[^:"])+', text)]


def load(path):
    """[(line number, [statement, ...]), ...] in line order."""
    lines = []
    for raw in open(path).read().split("\n"):
        m = re.match(r"(\d+) (.*)", raw)
        if m:
            lines.append((int(m.group(1)), split_statements(m.group(2))))
        elif raw.strip() and not raw.startswith("REM "):
            raise Problem("%s: not a program line: %r" % (path, raw))
    return sorted(lines)


def evaluate(expr, variables):
    """An integer expression: numbers, variables, + - * / and parentheses."""
    tokens = re.findall(r"\d+|[A-Z][A-Z0-9]*|[-+*/()=<>#]", expr)
    if "".join(tokens) != expr.replace(" ", ""):
        raise Problem("cannot read expression %r" % expr)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def primary():
        nonlocal pos
        t = peek()
        pos += 1
        if t == "(":
            v = compare()
            pos += 1
            return v
        if t == "-":
            return -primary()
        if t is None:
            raise Problem("expression ends early: %r" % expr)
        if t.isdigit():
            return int(t)
        if t not in variables:
            raise Problem("variable %s used before it is set" % t)
        return variables[t]

    def product():
        nonlocal pos
        v = primary()
        while peek() in ("*", "/"):
            op = tokens[pos]
            pos += 1
            w = primary()
            v = v * w if op == "*" else int(v / w)
        return v

    def total():
        nonlocal pos
        v = product()
        while peek() in ("+", "-"):
            op = tokens[pos]
            pos += 1
            w = product()
            v = v + w if op == "+" else v - w
        return v

    def compare():
        nonlocal pos
        v = total()
        while peek() in ("=", "<", ">", "#"):
            op = tokens[pos]
            pos += 1
            w = total()
            v = int({"=": v == w, "<": v < w, ">": v > w, "#": v != w}[op])
        return v

    v = compare()
    if pos != len(tokens):
        raise Problem("cannot read expression %r" % expr)
    return v


def run(path):
    """Run one carol; return its events."""
    lines = load(path)
    index = {number: k for k, (number, _) in enumerate(lines)}
    variables = {}
    events = []
    length = None           # the last value POKEd into 2057
    row, col = 1, 1
    loops = []              # [variable, limit, (line, statement) of the body, events at entry]
    returns = []
    li, si = 0, 0
    steps = 0

    def goto(expr):
        target = evaluate(expr, variables)
        if target not in index:
            raise Problem("%s: GOTO or GOSUB %d: no such line" % (path, target))
        return index[target], 0

    while li < len(lines):
        number, statements = lines[li]
        if si >= len(statements):
            li, si = li + 1, 0
            continue
        s = statements[si]
        si += 1
        steps += 1
        if steps > 2_000_000:
            raise Problem("%s: still running after 2,000,000 statements" % path)

        # IF: when the condition is false Integer BASIC skips only the
        # statement after THEN, not the rest of the line.
        m = re.match(r"IF (.+?) THEN (.+)", s)
        if m:
            if not evaluate(m.group(1), variables):
                continue
            s = m.group(2)
            if s.isdigit():
                s = "GOTO " + s

        word, _, rest = s.partition(" ")
        if word in ("TEXT", "REM"):
            pass
        elif word == "END":
            break
        elif word == "CALL":
            address = evaluate(rest, variables)
            if address == HOME:
                events.append(("clear",))
                row, col = 1, 1
            elif address in NOTES:
                if length is None:
                    raise Problem("%s line %d: a note before any length is set" % (path, number))
                if not 1 <= length - 1 <= 99:
                    raise Problem("%s line %d: note length %d" % (path, number, length))
                events.append(("note", NOTES[address], length - 1))
            else:
                raise Problem("%s line %d: CALL %d is not a note" % (path, number, address))
        elif word == "POKE":
            address, value = (evaluate(e, variables) for e in rest.split(","))
            if address == LENGTH_ADDRESS:
                length = value
            elif address not in (74, 75, 204, 205):       # where variables start
                raise Problem("%s line %d: POKE %d" % (path, number, address))
        elif word == "PRINT":
            m = re.fullmatch(r'"([^"]*)"', rest)
            if not m:
                raise Problem("%s line %d: PRINT of something other than one string" % (path, number))
            if not m.group(1).startswith("<CTRL-D>"):     # else a DOS command
                if row > 24 or col + len(m.group(1)) - 1 > 40:
                    raise Problem("%s line %d: text runs off the screen" % (path, number))
                events.append(("text", row, col, m.group(1)))
                row, col = row + 1, 1
        elif word == "VTAB":
            row = evaluate(rest, variables)
        elif word == "TAB":
            col = evaluate(rest, variables)
        elif word == "GOTO":
            li, si = goto(rest)
        elif word == "GOSUB":
            returns.append((li, si))
            li, si = goto(rest)
        elif word == "RETURN":
            li, si = returns.pop()
        elif word == "FOR":
            m = re.fullmatch(r"([A-Z][A-Z0-9]*)=(.+) TO (.+)", rest)
            variables[m.group(1)] = evaluate(m.group(2), variables)
            loops.append([m.group(1), evaluate(m.group(3), variables), (li, si), len(events), 1])
        elif word == "NEXT":
            while loops[-1][0] != rest:
                loops.pop()
            loop = loops[-1]
            variables[rest] += 1
            if variables[rest] <= loop[1]:
                loop[4] += 1
                li, si = loop[2]
            else:
                loops.pop()
                if loop[3] == len(events):                # nothing happened inside: a pause
                    events.append(("rest", round(loop[4] * LOOP_MS)))
        elif re.match(r"[A-Z][A-Z0-9]*=", s):
            name, _, expr = s.partition("=")
            variables[name] = evaluate(expr, variables)
        else:
            raise Problem("%s line %d: statement not understood: %s" % (path, number, s))
    return events


# ---- what comes out -------------------------------------------------------------

def carols(disk):
    """{number: events} for every carol on the disk."""
    found = {}
    for n in range(1, len(TITLES) + 1):
        path = os.path.join(disk, "A%d.bas" % n)
        if os.path.exists(path):
            found[n] = run(path)
    return found


def duration_ms(events):
    return sum(e[2] * UNIT_MS if e[0] == "note" else e[1] if e[0] == "rest" else 0 for e in events)


def screens(events):
    """How many times the screen is filled: runs of events between clears that have notes."""
    count, any_note = 0, False
    for e in events + [("clear",)]:
        if e[0] == "clear":
            count, any_note = count + any_note, False
        any_note = any_note or e[0] == "note"
    return count


FIRST_LINE = 10000          # carol n's events start at line FIRST_LINE + n * LINES_EACH
LINES_EACH = 1000
WIDTH = 100                 # of a DATA line


def data_lines(found):
    """The program lines, as (number, text)."""
    out = [(9000, "DATA %d" % len(TITLES))]
    out += [(9000 + n, 'DATA "%s"' % title) for n, title in enumerate(TITLES, 1)]

    # GOSUB 9500: point READ at carol number `carol`.  RESTORE takes only a
    # line number written out, hence one line per carol.
    half = (len(TITLES) + 1) // 2
    targets = [str(9600 + n) for n in range(1, len(TITLES) + 1)]
    out.append((9500, "IF carol > %d THEN 9520" % half))
    out.append((9510, "ON carol GOTO " + ",".join(targets[:half])))
    out.append((9520, "ON carol - %d GOTO %s" % (half, ",".join(targets[half:]))))
    for n in range(1, len(TITLES) + 1):
        out.append((9600 + n, "RESTORE %d: RETURN" % (FIRST_LINE + n * LINES_EACH)))

    for n in range(1, len(TITLES) + 1):
        items = []
        for e in found.get(n, []):
            if e[0] == "note":
                items.append(str(e[1] * 100 + e[2]))
            elif e[0] == "rest":
                items.append(str(-e[1]))
            elif e[0] == "text":
                items.append('0,%d,%d,"%s"' % e[1:])
            else:
                items.append("1")
        items.append("2" if n in found else "3")
        number = FIRST_LINE + n * LINES_EACH
        out.append((number, "REM %d %s" % (n, TITLES[n - 1])))
        line = ""
        for item in items:
            if line and len(line) + 1 + len(item) > WIDTH:
                number += 1
                out.append((number, "DATA " + line))
                line = ""
            line += ("," if line else "") + item
        out.append((number + 1, "DATA " + line))
        if number + 1 >= FIRST_LINE + (n + 1) * LINES_EACH:
            raise Problem("carol %d needs more than %d lines" % (n, LINES_EACH))
    return out


def summary(found):
    print(" #  notes  screens  low high  m:ss   title")
    total = 0
    for n, title in enumerate(TITLES, 1):
        if n not in found:
            print("%2d  (not on the disk)             %s" % (n, title))
            continue
        events = found[n]
        notes = [e for e in events if e[0] == "note"]
        ms = duration_ms(events)
        total += ms
        print("%2d  %5d  %7d  %3d  %3d  %d:%02d   %s" % (
            n, len(notes), screens(events),
            min(e[1] for e in notes), max(e[1] for e in notes), ms // 60000, ms // 1000 % 60, title))
    print("all carols: %d min %02d s of notes and rests" % (total // 60000, total // 1000 % 60))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    try:
        found = carols(argv[1])
        if len(argv) == 2:
            summary(found)
        elif argv[2] == "--trace" and len(argv) == 4:
            for e in found[int(argv[3])]:
                print(*e)
        elif argv[2] == "--data" and len(argv) == 4:
            with open(argv[3], "w") as f:
                for number, text in data_lines(found):
                    f.write("%d %s\n" % (number, text))
        else:
            sys.exit(__doc__)
    except Problem as p:
        sys.exit("carols.py: %s" % p)


if __name__ == "__main__":
    main(sys.argv)

#!/usr/bin/env python3
"""Check the interpreter against a tree of recovered Apple II BASIC programs.

usage: tools/corpus.py UNPACKED_ROOT [--basic build/basic] [--tier NAME] [--run] [-v]

Finds every .bas file under UNPACKED_ROOT (as written by the unpacker in
apple2-disks-unpacked/_tools), keeps one copy of each distinct Applesoft
program, sorts them into tiers by what they need, and runs "basic --check" on
each.  Prints a table: programs per tier, how many compile cleanly, how many
contain statements that are recognised but not available yet, and how many
contain statements that do not parse.

  text      no graphics, PEEK, POKE, WAIT, CALL, USR or &
  graphics  lo-res or hi-res statements, nothing lower-level
  memory    PEEK, POKE or WAIT
  machine   CALL, USR or &

--tier limits the run to one tier; -v lists every statement that fails.

--run runs each program instead, with no input, no window, and a stop after
100,000 loop iterations, and counts how each one stopped.  For a program
stopped by ?NOT AVAILABLE ERROR it counts what was missing, which is what to
do next.  -v lists every program with how it stopped.

tools/corpus-known.txt names the programs whose errors are their own (damaged
or mistyped on the original disk).  The exit status is 1 if a text-tier
program that is not named there fails to parse.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

GRAPHICS = re.compile(r"\b(GR|HGR2?|PLOT|HLIN|VLIN|HPLOT|X?DRAW|SHLOAD)\b|COLOR=|ROT=|SCALE=|SCRN\(")
MEMORY = re.compile(r"\b(PEEK|POKE|WAIT)\b")
MACHINE = re.compile(r"\b(CALL|USR)\b|&")
TIERS = ("text", "graphics", "memory", "machine")


def code_only(text):
    """Program text without string literals, REM remarks and DATA items."""
    out = []
    for line in text.splitlines():
        line = re.sub(r'"[^"]*"?', '""', line)
        line = re.sub(r"\bREM\b.*", "", line)
        line = re.sub(r"\bDATA\b[^:]*", "", line)
        out.append(line)
    return "\n".join(out)


def tier_of(text):
    code = code_only(text)
    if MACHINE.search(code):
        return "machine"
    if MEMORY.search(code):
        return "memory"
    if GRAPHICS.search(code):
        return "graphics"
    return "text"


def programs(root):
    """One path per distinct Applesoft program, in a stable order."""
    seen = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in ("raw", "_tools"))
        for name in sorted(filenames):
            if not name.endswith(".bas"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="latin-1") as f:
                text = f.read()
            if not text.startswith("REM APPLESOFT BASIC"):
                continue
            digest = hashlib.sha1(text.encode("latin-1")).hexdigest()
            seen.setdefault(digest, (path, text))
    return list(seen.values())


def run_all(args):
    """Run every program headless and say how each stopped."""
    stops = {}                                     # (tier, how) -> count
    missing = {}                                   # what was not available -> count
    for path, text in programs(args.root):
        tier = tier_of(text)
        if args.tier and tier != args.tier:
            continue
        # perl supplies the timeout, as in tests/run.sh.
        command = ["perl", "-e", "alarm 30; exec @ARGV", args.basic, "--stream", "--no-window",
                   "--break-after", "100000", path]
        run = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True)
        out = (run.stdout + run.stderr).decode("latin-1")
        errors = re.findall(r"\?([A-Z' ]+) ERROR", out)
        if run.returncode < 0 or run.returncode > 128:
            how = "killed (hung or crashed)"
        elif errors:
            how = errors[-1]
        elif "BREAK" in out:
            how = "running, or waiting for a key"
        else:
            how = "ended"
        what = ""
        if how == "NOT AVAILABLE":
            notes = re.findall(r"\[((?:PEEK|POKE|CALL|MOVE TO|DOS COMMAND) [^\]]*)\]", out)
            what = notes[-1] if notes else "a statement"
            if what.startswith("CALL"):
                address = int(what.split()[1])
                what = "CALL into RAM" if address < 0xC000 else what
            missing[what] = missing.get(what, 0) + 1
        stops[(tier, how)] = stops.get((tier, how), 0) + 1
        if args.verbose:
            print("%-9s %-30s %-22s %s" % (tier, how, what, os.path.relpath(path, args.root)))
    if args.verbose:
        print()
    for (tier, how), count in sorted(stops.items()):
        print("%-9s %-30s %4d" % (tier, how, count))
    if missing:
        print("\nnot available:")
        for what, count in sorted(missing.items(), key=lambda item: (-item[1], item[0])):
            print("%4d  %s" % (count, what))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--basic", default="build/basic")
    ap.add_argument("--tier", choices=TIERS)
    ap.add_argument("--run", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    if args.run:
        return run_all(args)

    counts = {t: [0, 0, 0, 0] for t in TIERS}      # total, clean, unavailable, errors
    failures = []
    for path, text in programs(args.root):
        tier = tier_of(text)
        if args.tier and tier != args.tier:
            continue
        run = subprocess.run([args.basic, "--check", path], capture_output=True, text=True, errors="replace")
        lines = run.stdout.splitlines()
        if run.returncode not in (0, 1) or run.stderr:
            lines.append("?: interpreter failed: " + run.stderr.strip())
        bad = [ln for ln in lines if not ln.endswith(": NOT AVAILABLE")]
        counts[tier][0] += 1
        if bad:
            counts[tier][3] += 1
            failures.append((tier, path, bad))
        elif lines:
            counts[tier][2] += 1
        else:
            counts[tier][1] += 1

    known = set()
    known_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus-known.txt")
    if os.path.exists(known_file):
        with open(known_file) as f:
            known = {ln.split("#")[0].strip() for ln in f} - {""}

    print("%-9s %8s %8s %12s %8s" % ("tier", "programs", "clean", "unavailable", "errors"))
    for t in TIERS:
        if counts[t][0]:
            print("%-9s %8d %8d %12d %8d" % ((t,) + tuple(counts[t])))
    unexpected = 0
    for tier, path, bad in failures:
        rel = os.path.relpath(path, args.root)
        note = ", known" if rel in known else ""
        if tier == "text" and rel not in known:
            unexpected += 1
        print("\n%s  [%s%s]" % (rel, tier, note))
        for ln in bad if args.verbose else bad[:3]:
            print("    line " + ln)
        if not args.verbose and len(bad) > 3:
            print("    ... and %d more" % (len(bad) - 3))
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())

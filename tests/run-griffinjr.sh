#!/bin/sh
# Run the msbasic tests on Griffin Jr., in its emulator, and compare each
# one's output with the same NAME.out the host build is held to.
#
# usage: tests/run-griffinjr.sh [--bench] [NAME...]
#
# Needs build/griffinjr/BASIC.ELF (make -C platform/griffinjr) and the Griffin
# Jr. tree with its firmware and emulator built; JR says where it is.
#
# The interpreter and the test programs go on a C: volume behind the BIOS.
# For each test the emulator boots, "basic -s --marks ..." is typed at the
# monitor's prompt, and the test's input is sent to serial port A, which -s
# makes the interpreter's console.  The emulator's standard output is that
# port; what lies between the STX and EOT that --marks sends is the test's
# output.  NAME.cmd tests are direct-mode sessions: the file is the input.
#
# With NAMEs, only those tests are run.  --bench runs tests/bench instead and
# prints how long each program took in emulated seconds (see tests/bench).

cd "$(dirname "$0")/.." || exit 2
JR=${JR:-$HOME/trees/other-m68k}
EMU=$JR/emu/build/m68k-emu
ROM=$JR/fw/build/rom.bin
ELF=build/griffinjr/BASIC.ELF
B=build/griffinjr/test
for f in "$EMU" "$ROM" "$ELF"; do
    [ -f "$f" ] || { echo "run-griffinjr.sh: $f is missing" >&2; exit 2; }
done

bench=
if [ "$1" = "--bench" ]; then bench=1; shift; fi
if [ -n "$bench" ]; then
    tests=$(ls tests/bench/*.bas)
else
    tests=$(ls tests/programs/*.bas tests/lang/*.bas tests/lang/*.cmd)
fi
if [ $# -gt 0 ]; then
    all=$tests; tests=
    for name in "$@"; do
        for t in $all; do
            b=$(basename "$t"); [ "${b%.*}" = "$name" ] && tests="$tests $t"
        done
    done
fi

# The volume: the interpreter and every program, under upper-case names.
rm -rf "$B"; mkdir -p "$B/vol"
cp "$ELF" "$B/vol/BASIC.ELF"
for t in $tests; do
    case $t in *.bas) cp "$t" "$B/vol/$(basename "$t" | tr '[:lower:]' '[:upper:]')" ;; esac
done
sh "$JR/tools/mkflashvol.sh" "$B/c.img" "$B"/vol/* > "$B/mkvol.log" 2>&1 &&
python3 "$JR/fw/mkrom.py" --split "$B/flash.img" "$B/even.bin" "$B/odd.bin" "$ROM" "$B/c.img" > "$B/mkrom.log" 2>&1 ||
    { cat "$B/mkvol.log" "$B/mkrom.log"; echo "run-griffinjr.sh: could not build the flash image" >&2; exit 2; }

# run_emu COMMAND INPUTFILE OUT: boot, type COMMAND, send INPUTFILE and a
# Ctrl-D down the serial port, and write what comes back between the marks to
# OUT with the CRs removed.  The emulator is stopped at the EOT, or after
# LIMIT seconds.  Prints the emulated seconds between STX and EOT.
run_emu() {
    perl -e '
        my ($emu, $rom, $cmd, $infile, $out, $limit) = @ARGV;
        my $input = "";
        if (open(my $f, "<", $infile)) { local $/; $input = <$f>; close $f; }
        $input .= "\n" if length($input) && $input !~ /\n\z/;
        $input =~ s/\\n/\\\\n/g;       # the emulator reads \n as Return
        $input .= "\x04";
        my $pid = open(my $p, "-|", $emu, "--rom", $rom, "--headless", "--fast", "--no-stdin",
            "--type", $cmd . "\\n", "--type-delay", "350",
            "--serial-type", $input, "--serial-type-delay", "350") or die "cannot run $emu\n";
        binmode $p;
        $SIG{ALRM} = sub { kill "TERM", $pid; print STDERR "timeout\n"; exit 3; };
        alarm $limit;
        my ($buf, $c) = ("");
        while (sysread($p, $c, 4096)) { $buf .= $c; last if $buf =~ /\x02.*\x04/s; }
        kill "TERM", $pid;
        close $p;
        my $text = $buf =~ /\x02(.*?)\x04/s ? $1 : $buf;
        $text =~ s/\r//g;
        open(my $o, ">", $out) or die; binmode $o; print $o $text; close $o;
        exit($buf =~ /\x02.*\x04/s ? 0 : 4);
    ' "$EMU" "$B/flash.img" "$1" "$2" "$3" "${LIMIT:-300}" 2> "$B/emu.log"
}

failed=0
count=0
for t in $tests; do
    base=${t%.*}
    name=$(basename "$base")
    args=
    if [ -f "$base.args" ]; then args=$(cat "$base.args"); fi
    case $t in
        *.cmd) input=$t; prog= ;;
        *) input=$base.in; prog=$(basename "$t" | tr '[:lower:]' '[:upper:]') ;;
    esac
    out=$B/$name.actual
    run_emu "basic -s --marks ${bench:+--time} $args $prog" "$input" "$out"
    status=$?
    count=$((count + 1))
    if [ -n "$bench" ]; then
        echo "== $name"; cat "$out"
    elif [ $status -ne 0 ]; then
        echo "FAIL $t: the emulator run did not finish (status $status): $out"
        failed=$((failed + 1))
    elif cmp -s "$base.out" "$out"; then
        :
    else
        echo "FAIL $t: diff $base.out $out"
        failed=$((failed + 1))
    fi
done
if [ -z "$bench" ]; then echo "$((count - failed)) of $count tests passed on Griffin Jr."; fi
[ "$failed" -eq 0 ]

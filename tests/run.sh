#!/bin/sh
# Run every test and compare its output with the recorded one.
#
# usage: tests/run.sh path/to/basic [--update]
#
# A test is one of:
#   NAME.bas   a program, run as "basic [args] NAME.bas < NAME.in"
#   NAME.cmd   a direct-mode session, run as "basic [args] < NAME.cmd"
# with optional NAME.in (standard input, default empty) and NAME.args (extra
# command-line arguments).  NAME.out is the expected output; --update rewrites
# it from the current output, which should then be read before it is trusted.
#
# Tests in tests/graphics, tests/corpus/pictures and tests/griffin/pictures
# also write the screen with --png, and that picture is compared with NAME.png
# (and should be looked at after an --update).  Tests in tests/griffin/sound
# write what they played with --wav, compared with NAME.wav (listen to it).

bin=$1
update=$2
cd "$(dirname "$0")/.." || exit 2
if [ ! -x "$bin" ]; then
    echo "usage: tests/run.sh path/to/basic [--update]" >&2
    exit 2
fi

failed=0
count=0
rm -rf build/scratch              # where Applesoft tests write their files
mkdir -p build/scratch
for t in tests/programs/*.bas tests/lang/*.bas tests/lang/*.cmd \
         tests/applesoft/*.bas tests/applesoft/*.cmd tests/corpus/*.bas \
         tests/integer/*.bas tests/integer/*.cmd \
         tests/graphics/*.bas tests/corpus/pictures/*.bas \
         tests/griffin/*.bas tests/griffin/*.cmd tests/griffin/pictures/*.bas \
         tests/griffin/sound/*.bas; do
    [ -f "$t" ] || continue
    base=${t%.*}
    args=
    if [ -f "$base.args" ]; then args=$(cat "$base.args"); fi
    png=
    wav=
    case $t in
        tests/graphics/*|tests/corpus/pictures/*|tests/griffin/pictures/*) png=$base.actual.png; args="$args --png $png" ;;
        tests/griffin/sound/*) wav=$base.actual.wav; args="$args --wav $wav" ;;
    esac
    input=/dev/null
    case $t in
        *.cmd) input=$t; prog= ;;
        *) prog=$t; if [ -f "$base.in" ]; then input=$base.in; fi ;;
    esac
    # perl supplies the timeout: a hung interpreter must not hang the suite.
    # shellcheck disable=SC2086
    perl -e 'alarm 30; exec @ARGV' "$bin" $args $prog < "$input" > "$base.actual" 2>&1
    status=$?
    count=$((count + 1))
    if [ "$update" = "--update" ]; then
        mv "$base.actual" "$base.out"
        echo "updated $base.out"
        if [ -n "$png" ]; then mv "$png" "$base.png"; fi
        if [ -n "$wav" ]; then mv "$wav" "$base.wav"; fi
    elif [ -n "$png" ] && ! cmp -s "$png" "$base.png"; then
        echo "FAIL $t: picture differs: $base.png $png"
        failed=$((failed + 1))
    elif [ -n "$wav" ] && ! cmp -s "$wav" "$base.wav"; then
        echo "FAIL $t: sound differs: $base.wav $wav"
        failed=$((failed + 1))
    elif [ ! -f "$base.out" ]; then
        echo "FAIL $t: no expected output ($base.out); see $base.actual"
        failed=$((failed + 1))
    elif cmp -s "$base.out" "$base.actual"; then
        rm -f "$base.actual" $png $wav
    else
        echo "FAIL $t (exit $status): diff $base.out $base.actual"
        failed=$((failed + 1))
    fi
done

if [ "$update" != "--update" ]; then
    echo "$((count - failed)) of $count tests passed"
fi
[ "$failed" -eq 0 ]

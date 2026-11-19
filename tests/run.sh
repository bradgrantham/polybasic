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
# Tests in tests/graphics and tests/corpus/pictures also write the screen with
# --png, and that picture is compared with NAME.png (and should be looked at
# after an --update).

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
         tests/graphics/*.bas tests/corpus/pictures/*.bas; do
    [ -f "$t" ] || continue
    base=${t%.*}
    args=
    if [ -f "$base.args" ]; then args=$(cat "$base.args"); fi
    png=
    case $t in tests/graphics/*|tests/corpus/pictures/*) png=$base.actual.png; args="$args --png $png" ;; esac
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
    elif [ -n "$png" ] && ! cmp -s "$png" "$base.png"; then
        echo "FAIL $t: picture differs: $base.png $png"
        failed=$((failed + 1))
    elif [ ! -f "$base.out" ]; then
        echo "FAIL $t: no expected output ($base.out); see $base.actual"
        failed=$((failed + 1))
    elif cmp -s "$base.out" "$base.actual"; then
        rm -f "$base.actual" $png
    else
        echo "FAIL $t (exit $status): diff $base.out $base.actual"
        failed=$((failed + 1))
    fi
done

if [ "$update" != "--update" ]; then
    echo "$((count - failed)) of $count tests passed"
fi
[ "$failed" -eq 0 ]

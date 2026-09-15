#!/bin/sh -e
# test-shortcircuit.sh — L1 short-circuit gate for qmap_expr_eval
# (AXIS-EFF plan L1): once an E_AND/E_SUB running set is empty (∅∩X=∅,
# ∅∖X=∅) the remaining branches are skipped WITHOUT decoding their leaves
# — so e.g. a (text AND sepal) scan pays no sepal embed HTTP call when the
# text branch already empties the conjunction.
#
# Probe axis: lib/librec_axis_probe.so ("pe") — decode(value) emits
# "AXIS-BOOM-DECODED" on stderr for the value "boom"; fill() returns the
# empty set for the value "empty", else refs {1,2,3}. Result-exactness is
# also pinned: skipping never changes the printed rows.

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
qmap=./bin/qmap
probe=./lib/librec_axis_probe.so

fail=0

marker_grep() {
	# usage: marker_grep <name> <want(0=absent,1=present)> [args...]
	name=$1
	want=$2
	shift 2
	if "$qmap" "$@" >"$td/out" 2>"$td/err"; then
		rc=0
	else
		rc=$?
	fi
	if [ "$rc" -ne 0 ]; then
		echo "FAIL - $name (qmap exitted $rc)"
		fail=1
		return
	fi
	if grep -q 'AXIS-BOOM-DECODED' "$td/err"; then
		got=1
	else
		got=0
	fi
	if [ "$want" -eq "$got" ]; then
		echo "ok - $name"
	else
		echo "FAIL - $name (marker want=$want got=$got)"
		cat "$td/err"
		fail=1
	fi
}

echo "=== seeding probe.db (refs 1..3) ==="
"$qmap" -p 1:a -p 2:b -p 3:c "$td/probe.db:a:s" >/dev/null

export QMAP_AXIS_LIBS=$PWD/$probe
export QMAP_AXIS_PATH=./lib
filespec="$td/probe.db@pe:a:s"

echo "=== AND: empty first → boom branch skipped (no decode marker) ==="
marker_grep "and-empty-first-skips" 0 \
	-X '(pe="empty" AND pe="boom")' -g . "$filespec"

echo "=== AND control: boom first → leaf still decodes ==="
marker_grep "and-control-boom-first-decodes" 1 \
	-X '(pe="boom" AND pe="empty")' -g . "$filespec"

echo "=== EXCEPT: empty left → right skipped ==="
marker_grep "except-empty-left-skips" 0 \
	-X '(pe="empty" EXCEPT pe="boom")' -g . "$filespec"

echo "=== EXCEPT control: non-empty left → right decodes ==="
marker_grep "except-control-right-decodes" 1 \
	-X '(pe="zhit" EXCEPT pe="boom")' -g . "$filespec"

echo "=== result-exactness: empty AND boom prints no rows, exit 0 ==="
out=$("$qmap" -X '(pe="empty" AND pe="boom")' -g . "$filespec" 2>/dev/null)
if [ -z "$out" ]; then
	echo "ok - empty-and result empty"
else
	echo "FAIL - empty-and result not empty: $out"
	fail=1
fi

echo "=== result-exactness: plain zhit AND zhit still rows 1..3 ==="
out=$("$qmap" -X '(pe="zhit" AND pe="zhit")' -g . "$filespec")
expected="1 a
2 b
3 c"
if [ "$out" = "$expected" ]; then
	echo "ok - zhit-and rows intact"
else
	echo "FAIL - zhit-and rows changed"
	echo "  expected: $(printf '%s' "$expected" | tr '\n' '|')"
	echo "  actual:   $(printf '%s' "$out" | tr '\n' '|')"
	fail=1
fi

exit $fail
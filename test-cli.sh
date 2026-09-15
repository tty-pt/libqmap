#!/bin/sh -e
# test-cli.sh — end-to-end test for the 2B-3 composed -X/-g . fold
# (mm-plan/2B-3-IMPLEMENTATION.md §7). Exercises the whole composed path:
# -X set-expression parsing (AND/OR/NOT/parens/prefix-NOT), D9 by-name +
# env-lib axis loading, composed render (ref + score + record), pure-filter,
# --top/--bottom, --list-axes (with @ roster and standalone), dangling-ref
# skip, and the classic flat CLI regression — against the functional
# lib/librec_axis_fold.so plugin (alpha/beta/pure over real qmap a:u stores).
# test-roster.sh (2B-1 sidecar behavior) still passes independently.

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
qmap=./bin/qmap
fold=./lib/librec_axis_fold.so

fail=0

assert_eq() {
	name=$1
	expected=$2
	actual=$3
	if [ "$expected" = "$actual" ]; then
		echo "ok - $name"
	else
		echo "FAIL - $name"
		echo "  expected: $(printf '%s' "$expected" | tr '\n' '|')"
		echo "  actual:   $(printf '%s' "$actual" | tr '\n' '|')"
		fail=1
	fi
}

assert_fails() {
	name=$1
	expects=$2
	shift 2
	if "$qmap" "$@" >/dev/null 2>"$td/stderr"; then
		echo "FAIL - $name (expected nonzero exit)"
		fail=1
	elif grep -q "$expects" "$td/stderr"; then
		echo "ok - $name"
	else
		echo "FAIL - $name (stderr lacks '$expects')"
		echo "  stderr: $(cat "$td/stderr")"
		fail=1
	fi
}

echo "=== seeding (envs clean) ==="
# Axis fills are per-primary (7-AXIS-NAMESPACE-PLAN.md): each is seeded as
# the primary-owned alongside store <primary>-<axis>, e.g. demo.db-alpha.
"$qmap" -p 1:one -p 2:two -p 3:three -p 4:four "$td/demo.db:a:s" >/dev/null
"$qmap" -p 1:r "$td/roster.db:a:s" >/dev/null
"$qmap" -p 1:1 -p 2:2 -p 3:3 "$td/demo.db-alpha:a:u" >/dev/null
"$qmap" -p 2:2 -p 3:3 -p 4:4 "$td/demo.db-beta:a:u" >/dev/null
"$qmap" -p 1:1 -p 2:2 "$td/demo.db-pure:a:u" >/dev/null

export QMAP_AXIS_LIBS=$PWD/$fold
export QMAP_AXIS_PATH=./lib

echo "=== --list-axes with @ roster ==="
out=$("$qmap" --list-axes "$td/roster.db@alpha,beta,pure:a:s")
expected="slot  name              fill  rank  ctx
0     alpha             y     y     y
1     beta              y     y     y
2     pure              y     n     y"
assert_eq "list-axes-with-at" "$expected" "$out"

echo "=== --list-axes standalone (no file) ==="
out=$("$qmap" --list-axes)
expected="slot  name              fill  rank  ctx
0     alpha             y     y     n
1     beta              y     y     n
2     pure              y     n     n"
assert_eq "list-axes-standalone" "$expected" "$out"

echo "=== -X bare name: alpha {1,2,3} ==="
out=$("$qmap" -X alpha -g . "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "bare-name" "$expected" "$out"

echo "=== -X AND: alpha AND beta = {2,3} ==="
out=$("$qmap" -X "alpha AND beta" -g . "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two"
assert_eq "and" "$expected" "$out"

echo "=== -X OR: alpha OR beta = {1,2,3,4} ==="
out=$("$qmap" -X "alpha OR beta" -g . "$td/demo.db:a:s")
expected="4 4.000000 four
3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "or" "$expected" "$out"

echo "=== -X EXCEPT (setminus): alpha EXCEPT beta = {1} ==="
out=$("$qmap" -X "alpha EXCEPT beta" -g . "$td/demo.db:a:s")
expected="1 1.000000 one"
assert_eq "except" "$expected" "$out"

echo "=== -X EXCEPT chains left-to-right: (alpha EXCEPT beta) EXCEPT pure = {} ==="
out=$("$qmap" -X "alpha EXCEPT beta EXCEPT pure" -g . "$td/demo.db:a:s")
expected=""
assert_eq "except-chain" "$expected" "$out"

echo "=== -X prefix NOT: complement vs primary universe = {4} ==="
out=$("$qmap" -X "NOT alpha" -g . "$td/demo.db:a:s")
expected="4 4.000000 four"
assert_eq "not-prefix" "$expected" "$out"

echo "=== -X parens + precedence: (alpha OR beta) AND pure = {1,2} ==="
out=$("$qmap" -X "(alpha OR beta) AND pure" -g . "$td/demo.db:a:s")
expected="2 2.000000 two
1 1.000000 one"
assert_eq "parens-and" "$expected" "$out"

echo "=== -X precendence: alpha OR (beta AND pure) = {1,2,3} ==="
out=$("$qmap" -X "alpha OR (beta AND pure)" -g . "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "parens-or" "$expected" "$out"

echo "=== -X --top caps ==="
out=$("$qmap" -X "alpha OR beta" -g . "$td/demo.db:a:s" -t 2)
expected="4 4.000000 four
3 3.000000 three"
assert_eq "top-cap" "$expected" "$out"

echo "=== -X --bottom floors ==="
out=$("$qmap" -X "alpha OR beta" -g . "$td/demo.db:a:s" -b 2.5)
expected="4 4.000000 four
3 3.000000 three"
assert_eq "bottom-floor" "$expected" "$out"

echo "=== -X pure filter (no rank fn): asc refs, no score ==="
out=$("$qmap" -X pure -g . "$td/demo.db:a:s")
expected="1 one
2 two"
assert_eq "pure-filter" "$expected" "$out"

echo "=== -X NAME=VALUE: whole-string VALUE forwarded ==="
out=$("$qmap" -X "alpha=hello world" -g . "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "name-value" "$expected" "$out"

echo "=== -X empty string: unarmed, classic -g . ==="
# NOTE: classic -g . on an :a: primary prints refs (verified byte-identical
# against the pre-fold binary) — these rows pin the classic path unchanged.
out=$("$qmap" -X "" -g . "$td/demo.db:a:s")
expected="1
2
3
4"
assert_eq "unarmed-empty" "$expected" "$out"

echo "=== no -X: classic -g . ==="
out=$("$qmap" -g . "$td/demo.db:a:s")
expected="1
2
3
4"
assert_eq "classic-dot" "$expected" "$out"

echo "=== classic -g 1 ==="
out=$("$qmap" -g 1 "$td/demo.db:a:s")
assert_eq "classic-get" "-1" "$out"

echo "=== armed -X ignores -k ==="
out=$("$qmap" -k -X alpha -g . "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "ignore-k" "$expected" "$out"

echo "=== ops interleave: composed get then put ==="
out=$("$qmap" -X alpha -g . "$td/demo.db:a:s" -p 9:nine "$td/demo.db:a:s")
expected="3 3.000000 three
2 2.000000 two
1 1.000000 one
9"
assert_eq "interleave" "$expected" "$out"

echo "=== -X unknown axis name: named error, exit 1 ==="
assert_fails "unknown-axis" "unknown axis name" -X wonka -g . "$td/demo.db:a:s"

echo "=== -X malformed expr (unclosed paren): named parse error, exit 1 ==="
assert_fails "bad-expr" "expected ')'" -X "(alpha OR beta" -g . "$td/demo.db:a:s"

echo "=== -X infix NOT is a parse error (hint: use EXCEPT), exit 1 ==="
assert_fails "infix-not" "use EXCEPT" -X "alpha NOT beta" -g . "$td/demo.db:a:s"

echo "=== -X empty parens: named parse error, exit 1 ==="
assert_fails "empty-parens" "expected expression" -X "()" -g . "$td/demo.db:a:s"

echo "=== -t invalid value ==="
assert_fails "bad-top" "invalid --top value" -t abc -g 1 "$td/demo.db:a:s"

echo "=== -b invalid value ==="
assert_fails "bad-bottom" "invalid --bottom value" -b abc -g 1 "$td/demo.db:a:s"

echo "=== -X --top 0 = all (same as 4 lines) ==="
out=$("$qmap" -X "alpha OR beta" -g . "$td/demo.db:a:s" -t 0)
expected="4 4.000000 four
3 3.000000 three
2 2.000000 two
1 1.000000 one"
assert_eq "top-zero" "$expected" "$out"

echo "=== classic smoke with envs unset ==="
# NOTE: the interleave row above persisted ref 9 into demo.db, so the
# classic listing shows 5 refs. Pins the classic path byte-identical.
unset QMAP_AXIS_LIBS QMAP_AXIS_PATH
out=$("$qmap" -g . "$td/demo.db:a:s")
expected="1
2
3
4
9"
assert_eq "classic-smoke" "$expected" "$out"

if [ "$fail" -ne 0 ]; then
	echo "test-cli.sh: FAILURES ABOVE" >&2
	exit 1
fi
echo "test-cli.sh: all green"
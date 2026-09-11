#!/bin/sh -e
# test-cli.sh — end-to-end smoke test for `qmap -Q` (PLAN-REC-QUERY.md §4.4,
# Part 3.4). Exercises the full CLI plugin pipeline (qsys_dlopen, dlsym'd
# rec_axis_open, rec_axis_set_ctx, --axis/--params/--and/--or/--not/
# --combine/--top/--min/--list-axes) against a tiny, deterministic,
# self-contained mock axis plugin (lib/librec_axis_mock.so, two axes —
# "mock_a" fill+rank+decode, "mock_b" fill-only) — no real axis library or
# persisted data involved. This replaces the plan's original gate 8 (seed a
# stoma + libislet store, run bin/qsearch), deferred per PLAN-REC-QUERY.md
# §4.5 (no axis has real persisted production data today).

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
qmap=./bin/qmap
mock=./lib/librec_axis_mock.so

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

echo "=== -Q --list-axes ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' --list-axes)
expected="slot  name              fill  rank  ctx
0     mock_a            y     y     y
1     mock_b            y     n     y"
assert_eq "list-axes" "$expected" "$out"

echo "=== -Q AND (default join): seed {1,2,3} INTERSECT {2,3,4} = {2,3} ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --axis 1 --top 10)
expected="3 3.000000
2 2.000000"
assert_eq "and" "$expected" "$out"

echo "=== -Q OR: seed {1,2,3} UNION {2,3,4} = {1,2,3,4} ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --or --axis 1 --top 10)
expected="4 4.000000
3 3.000000
2 2.000000
1 1.000000"
assert_eq "or" "$expected" "$out"

echo "=== -Q NOT: seed {1,2,3} MINUS {2,3,4} = {1} ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --not --axis 1 --top 10)
expected="1 1.000000"
assert_eq "not" "$expected" "$out"

echo "=== -Q --combine or overrides the per-axis default AND ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --axis 1 --combine or --top 10)
expected="4 4.000000
3 3.000000
2 2.000000
1 1.000000"
assert_eq "combine-or" "$expected" "$out"

echo "=== -Q --min filters the AND result ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --axis 1 --min 2.5)
expected="3 3.000000"
assert_eq "min" "$expected" "$out"

echo "=== -Q --top caps the result count ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --or --axis 1 --top 2)
expected="4 4.000000
3 3.000000"
assert_eq "top" "$expected" "$out"

echo "=== -Q --params: decoded on mock_a (has decode), raw on mock_b (no decode) ==="
out=$("$qmap" -Q --dl "$mock" --open 'a=1,2,3:b=2,3,4' \
              --axis 0 --params hello --axis 1 --params world --top 10)
expected="3 3.000000
2 2.000000"
assert_eq "params" "$expected" "$out"

echo "=== -Q: missing --axis is an error ==="
if "$qmap" -Q --dl "$mock" >/dev/null 2>&1; then
	echo "FAIL - missing-axis-error (expected nonzero exit)"
	fail=1
else
	echo "ok - missing-axis-error"
fi

echo "=== -Q: bad --dl path warns but does not crash ==="
out=$("$qmap" -Q --dl ./no-such-plugin.so --list-axes 2>/dev/null)
assert_eq "bad-dl" "slot  name              fill  rank  ctx" "$out"

if [ "$fail" -ne 0 ]; then
	echo "test-cli.sh: FAILURES ABOVE" >&2
	exit 1
fi
echo "test-cli.sh: all green"

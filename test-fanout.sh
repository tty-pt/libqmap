#!/bin/sh -e
# test-fanout.sh — 2B-4 write fan-out + forget gate
# (mm-plan/PHASE-2-CLI.md 2B-4 with D11..D13).
#
# Exercises the CLI wiring only: -p/-d/-D fan-out to the @ roster as
# (ref, payload) — string rec_axis_store for :a:s primaries, additive
# rec_axis_store_typed when vtype != QM_STR (D12) else the string symbol
# (text-only fallback); loud partials (attempt all, report all, nonzero);
# idempotent forget (-d == -D on axes); QMAP_MASK shrink (D11).
# Writes are verified cross-process through the plugins' own
# rec_axis_readback via tests/fanout_verify.c, and refs are re-queried
# through -X (stash feeds fill).

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
qmap=./bin/qmap
fold=./lib/librec_axis_fold.so
plain=./lib/librec_axis_plain.so
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

assert_ok() {
	name=$1
	expected_exit=$2
	actual=$3
	if [ "$expected_exit" = "$actual" ]; then
		echo "ok - $name"
	else
		echo "FAIL - $name (expected exit=$expected_exit actual=$actual)"
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

echo "=== compile verifier ==="
cc -o "$td/fanout_verify" tests/fanout_verify.c -Iinclude -ldl \
	2>"$td/cc.err" || { cat "$td/cc.err"; echo "FAIL - verifier compile"; exit 1; }
echo "ok - verifier compiled"

export QMAP_AXIS_LIBS=$PWD/$fold:$PWD/$plain
export QMAP_AXIS_PATH=./lib

echo "=== string fan-out verbatim (:a:s) ==="
"$qmap" -p 1:hello -p 2:world "$td/demo.db@alpha,beta:a:s" >/dev/null
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 1 2)
assert_eq "alpha-stash-verbatim" "1:hello
2:world" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/beta.db" 1 2)
assert_eq "beta-stash-verbatim" "1:hello
2:world" "$out"

echo "=== -X answers written refs (stash feeds fill) ==="
out=$("$qmap" -X alpha -g . "$td/demo.db@alpha,beta:a:s")
assert_eq "x-answers-writes" "2 2.000000 world
1 1.000000 hello" "$out"

echo "=== reopen: stash persisted ==="
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 1 2)
assert_eq "stash-persisted" "1:hello
2:world" "$out"

echo "=== auto-ref put fans out (fresh db: auto refs are positions) ==="
out=$("$qmap" -p auto_value "$td/auto.db@alpha:a:s")
assert_eq "auto-ref-id" "0" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 0)
assert_eq "auto-ref-stash" "0:auto_value" "$out"

echo "=== typed dispatch on :a:u (typed symbol ran) ==="
"$qmap" -p 9:whatever "$td/num.db@alpha:a:u" >/dev/null
g=$("$td/fanout_verify" pu32 "$td/num.db" 9)
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 9)
assert_eq "typed-dispatch" "9:T:$g" "$out"

echo "=== text-only path (:a:s -> string symbol) ==="
"$qmap" -p 7:chars "$td/m.db@plain:a:s" >/dev/null
out=$("$td/fanout_verify" readback "$PWD/$plain" "$td/plain.db" 7)
assert_eq "text-verbatim" "7:chars" "$out"

echo "=== binary payload + text-only axis: loud skip, no store ==="
exit_code=0
"$qmap" -p 8:bytes "$td/u.db@plain:a:u" >/dev/null 2>"$td/textonly.err" \
	|| exit_code=$?
assert_ok "textonly-nonzero" "1" "$exit_code"
if grep -q "text-only" "$td/textonly.err"; then
	echo "ok - textonly-named"
else
	echo "FAIL - textonly-named (stderr lacks 'text-only')"
	echo "  stderr: $(cat "$td/textonly.err" | tr '\n' '|')"
	fail=1
fi
out=$("$td/fanout_verify" readback "$PWD/$plain" "$td/plain.db" 8)
assert_eq "textonly-unstored" "8:" "$out"

echo "=== named forget (-d NAME via reverse view) ==="
"$qmap" -d hello "$td/demo.db@alpha,beta:a:s"
assert_ok "named-forget-exit" "0" "$?"
out=$("$qmap" -g . "$td/demo.db:a:s")
assert_eq "named-forget-primary" "2" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 1)
assert_eq "named-forget-alpha" "1:" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/beta.db" 1)
assert_eq "named-forget-beta" "1:" "$out"

echo "=== forget (-d REF) + idempotent re-run ==="
"$qmap" -d 2 "$td/demo.db@alpha,beta:a:s"
assert_ok "forget-exit" "0" "$?"
out=$("$qmap" -g . "$td/demo.db:a:s")
assert_eq "forget-primary" "-1" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 2)
assert_eq "forget-alpha" "2:" "$out"
"$qmap" -d 2 "$td/demo.db@alpha,beta:a:s"
assert_ok "forget-idempotent" "0" "$?"

echo "=== -D collapses to unstore on axes ==="
"$qmap" -p 4:four "$td/demo.db@alpha:a:s" >/dev/null
"$qmap" -D 4 "$td/demo.db@alpha:a:s"
assert_ok "forget-all-exit" "0" "$?"
out=$("$qmap" -g . "$td/demo.db:a:s")
assert_eq "forget-all-primary" "-1" "$out"
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 4)
assert_eq "forget-all-alpha" "4:" "$out"

echo "=== loud partials: read-only target (no store symbol) ==="
assert_fails "readonly-partial" "read-only" -p 1:x "$td/r.db@stub:a:s"

echo "=== mixed partial: stored target still attempted, all reported ==="
"$qmap" -p 3:three "$td/demo2.db@alpha,stub:a:s" >/dev/null 2>"$td/mix.err" \
	|| true
if grep -q "stub" "$td/mix.err"; then
	echo "ok - mixed-partial-named"
else
	echo "FAIL - mixed-partial-named (stderr lacks 'stub')"
	echo "  stderr: $(cat "$td/mix.err" | tr '\n' '|')"
	fail=1
fi
out=$("$td/fanout_verify" readback "$PWD/$fold" "$td/alpha.db" 3)
assert_eq "mixed-partial-attempted" "3:three" "$out"
exit_code=0
"$qmap" -p 4:four "$td/demo2.db@alpha,stub:a:s" >/dev/null 2>&1 \
	|| exit_code=$?
assert_ok "mixed-partial-nonzero" "1" "$exit_code"

echo "=== non-:a: primary + roster: primary written, fan-out declined ==="
exit_code=0
"$qmap" -p k:v "$td/str.db@alpha:u:s" >/dev/null 2>"$td/nona.err" \
	|| exit_code=$?
assert_ok "nona-nonzero" "1" "$exit_code"
if grep -q ":a:-type primary" "$td/nona.err"; then
	echo "ok - nona-named"
else
	echo "FAIL - nona-named (stderr lacks ':a:-type primary')"
	echo "  stderr: $(cat "$td/nona.err" | tr '\n' '|')"
	fail=1
fi

echo "=== QMAP_MASK honored (D11) ==="
out=$(QMAP_MASK=255 "$qmap" -p 1:a "$td/mask.db:a:s")
assert_eq "mask-put" "1" "$out"
out=$(QMAP_MASK=255 "$qmap" -g . "$td/mask.db:a:s")
assert_eq "mask-reopen" "1" "$out"
export QMAP_MASK=100
assert_fails "bad-mask" "invalid QMAP_MASK" -p 1:a "$td/mask.db:a:s"
unset QMAP_MASK

echo "=== composed -d with unresolvable ref ==="
assert_fails "unknown-ref" "no primary record" -d wonka \
	"$td/demo.db@alpha:a:s"

echo "=== classic neutrality (no roster) — sanity ==="
out=$("$qmap" -p 5:five "$td/plain.db:a:s")
assert_eq "classic-put-id" "5" "$out"
out=$("$qmap" -g . "$td/plain.db:a:s")
assert_eq "classic-all" "5" "$out"
"$qmap" -p 6:six "$td/plain.db:a:s" >/dev/null
out=$("$qmap" -g . "$td/plain.db:a:s")
assert_eq "classic-all-2" "5
6" "$out"

if [ "$fail" -ne 0 ]; then
	echo "test-fanout.sh: FAILURES ABOVE" >&2
	exit 1
fi
echo "test-fanout.sh: all green"

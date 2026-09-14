#!/bin/sh -e
# test-roster.sh — roster/load tests (2B-1: TDD)
# Exercises: @roster parse, write-if-absent, stored-roster reload,
# override-once, unknown name → named error, QMAP_AXIS_LIBS, zero overhead.

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
export QMAP_AXIS_PATH=./lib
unset QMAP_AXIS_LIBS

qmap=./bin/qmap
fail=0

cleanup() {
	rm -rf "$td" "$td2"
}
trap cleanup EXIT
td=$(mktemp -d)
td2=$(mktemp -d)

assert_eq() {
	name=$1 expected=$2 actual=$3
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
	name=$1 expected_exit=$2 actual=$3
	if [ "$expected_exit" = "$actual" ]; then
		echo "ok - $name"
	else
		echo "FAIL - $name"
		echo "  expected exit=$expected_exit actual=$actual"
		fail=1
	fi
}

assert_contains() {
	name=$1 needle=$2 haystack=$3
	if printf '%s' "$haystack" | grep -q "$needle"; then
		echo "ok - $name"
	else
		echo "FAIL - $name"
		echo "  expected to contain: $needle"
		echo "  actual: $(printf '%s' "$haystack" | tr '\n' '|')"
		fail=1
	fi
}

# ── 1: write-if-absent: first @ with axes → sidecar created ──
echo "=== 1: write-if-absent ==="
primary="$td/tf.db"
"$qmap" -p 1:hello "$primary@stub,zed:a:s" >/dev/null 2>&1 || true
if [ -s "$primary.roster" ]; then
	echo "ok - sidecar-created"
else
	echo "FAIL - sidecar-created"
	fail=1
fi

# ── 2: stored-roster reload: reopen without @ → roster loads ──
echo "=== 2: stored-roster reload ==="
axes=$("$qmap" --list-axes "$primary:a:s" 2>/dev/null) || true
assert_contains "stored-reload-lists-stub" "stub" "$axes"
assert_contains "stored-reload-lists-zed" "zed" "$axes"

# ── 3: override-once: explicit @ replaces stored for this invocation only ──
echo "=== 3: override-once ==="
axes=$("$qmap" --list-axes "$primary@zed:a:s" 2>/dev/null) || true
assert_contains "override-lists-zed" "zed" "$axes"
if printf '%s' "$axes" | grep -q "stub"; then
	echo "FAIL - override-not-lists-stub"
	fail=1
else
	echo "ok - override-not-lists-stub"
fi
# Sidecar unchanged: reopen without @ still reloads stub,zed
after_override=$("$qmap" --list-axes "$primary:a:s" 2>/dev/null) || true
assert_contains "override-unchanged-stub" "stub" "$after_override"
assert_contains "override-unchanged-zed" "zed" "$after_override"

# ── 4: unknown name → named error ──
echo "=== 4: unknown name ==="
exit_code=0
"$qmap" --list-axes "$primary@wonka:a:s" >/dev/null 2>&1 || exit_code=$?
assert_ok "unknown-name-nonzero" "1" "$exit_code"

# ── 7: alongside-heuristic — axis store exists but no roster yet ──
echo "=== 7: alongside-heuristic ==="
primary_h="$td2/hint.db"
touch "$td2/stub.db"
hint_err=""
"$qmap" --list-axes "$primary_h@stub:a:s" 2>"$td2/hint.err" >/dev/null
hint_err=$(cat < "$td2/hint.err")
assert_contains "heuristic-hint" "roster" "$hint_err"

# ── 5: QMAP_AXIS_LIBS alongside by-name ──
echo "=== 5: QMAP_AXIS_LIBS alongside by-name ==="
export QMAP_AXIS_LIBS="$PWD/lib/librec_axis_mock.so"
primary2="$td2/ql.db"
"$qmap" -p 1:x "$primary2@mock_b:a:s" >/dev/null 2>&1 || true
axes=$("$qmap" --list-axes "$primary2:a:s" 2>/dev/null) || true
assert_contains "env-libs-resolve" "mock_b" "$axes"
unset QMAP_AXIS_LIBS

# ── 6: zero overhead — classic invocations unchanged ──
echo "=== 6: zero-overhead classic ==="
classic="$td2/classic.db"
"$qmap" -p 1:alpha "$classic:u:s" >/dev/null 2>&1
val=$("$qmap" -r -g 1 "$classic:u:s" 2>/dev/null)
assert_eq "classic-put-get" "alpha" "$val"

if [ "$fail" -ne 0 ]; then
	echo "test-roster.sh: FAILURES ABOVE" >&2
	exit 1
fi
echo "test-roster.sh: all green"
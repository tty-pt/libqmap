#!/bin/sh -e
# test-real.sh — 2B-2 real-file gate (mm-plan/PHASE-2-CLI.md 2B-2).
#
# Non-mm space∩time∩text over REAL primary+joint+islet+sepal files with the
# SAME refs (1/2/3), stoma rebuilt from the primary at each open (never
# files). Also asserts: plain -g answers with zero plugins on a fresh
# primary, sepal offline floats-direct (the string→embed→ANN column is
# skipped unless QMAP_SEPAL_EMBED_URL+MODEL are both set — D8), the
# stoma rebuild budget note is measured + recorded (mm-plan U4), and the
# 2B-6 D2 rank pin (first rank-capable leaf in preorder wins when two
# rankers share one -X expression: stoma over sepal cosine).
#
# Requires the four real axis .so's; resolved from $QMAP_AXIS_REAL_LIBS
# (colon list, default = the in-site submodule builds under
# external/lib{joint,islet,sepal,stoma}/lib, relative to this script).
# Fresh local builds only (D4) — nothing installed.

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
qmap=./bin/qmap
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

assert_contains() {
	name=$1
	needle=$2
	haystack=$3
	if printf '%s' "$haystack" | grep -q "$needle"; then
		echo "ok - $name"
	else
		echo "FAIL - $name (missing '$needle')"
		echo "  haystack: $(printf '%s' "$haystack" | tr '\n' '|')"
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

# --- resolve the real axis lib dirs (fresh local builds, D4) ---
# Default: the in-site submodule builds, relative to this script (explicit
# $QMAP_AXIS_REAL_LIBS still overrides).
script_dir=$(dirname "$0")
real_libs=${QMAP_AXIS_REAL_LIBS:-$script_dir/../libjoint/lib:$script_dir/../libislet/lib:$script_dir/../libsepal/lib:$script_dir/../libstoma/lib}
axis_path=""
oIFS=$IFS
IFS=:
for d in $real_libs; do
	[ -n "$d" ] || continue
	axis_path="$axis_path${axis_path:+:}$d"
done
IFS=$oIFS
export QMAP_AXIS_PATH=$axis_path
export LD_LIBRARY_PATH=$axis_path:$LD_LIBRARY_PATH

echo "=== preflight: real axis .so ==="
missing=0
for a in joint islet sepal stoma; do
	found=0
	oIFS2=$IFS
	IFS=:
	for d in $axis_path; do
		if [ -r "$d/lib$a.so" ]; then
			found=1
			break
		fi
	done
	IFS=$oIFS2
	if [ "$found" = "1" ]; then
		echo "ok - lib$a.so present"
	else
		echo "FAIL - lib$a.so: not found in QMAP_AXIS_PATH ($axis_path)"
		missing=1
	fi
done
if [ "$missing" = "1" ]; then
	echo "test-real.sh: REAL AXIS LIBRARIES MISSING (1 FAIL)"
	exit 1
fi

# --- compile the seeder against the real libs ---
echo "=== compile seeder ==="
cc -o "$td/real_seed" tests/real_seed.c -Iinclude -ldl 2>"$td/cc.err" \
	|| { cat "$td/cc.err"; echo "FAIL - seeder compile"; exit 1; }
echo "ok - seeder compiled"

echo "=== seeding (explicit refs 1/2/3 into primary + axes) ==="
"$qmap" -p 1:"2026-09-13T20:00:00:Beacon Harbor lights" \
        -p 2:"2026-09-15T08:00:00:alpha omega" \
        -p 3:"2026-09-14T12:00:00:Beacon Harbor lights" \
        "$td/greps.db:a:s" >/dev/null
"$qmap" -p 1:plain -p 2:records "$td/fresh.db:a:s" >/dev/null
"$td/real_seed" "$axis_path" "$td" 2>"$td/seed.err" || { cat "$td/seed.err"; echo "FAIL - seeding"; exit 1; }
echo "ok - seeded"

qvec="$td/q.vec"
qdim=$(cat "$td/q.dim")
if [ "$qdim" -ge 1 ] 2>/dev/null; then
	echo "ok - qvec dim $qdim"
else
	echo "FAIL - bad q.dim: $qdim"
	fail=1
fi

echo "=== embed column ==="
if [ -n "$QMAP_SEPAL_EMBED_URL" ] && [ -n "$QMAP_SEPAL_EMBED_MODEL" ]; then
	echo "embed mode (QMAP_SEPAL_EMBED_URL+MODEL set)"
else
	echo "ok - floats-direct column (offline default; embed skipped)"
fi

roster="$td/greps.db@joint,islet,sepal,stoma:a:s"

echo "=== first @ invocation: loud, never silent ==="
out=$("$qmap" --list-axes "$roster" 2>"$td/list1.err")
echo "--- prime stderr ---"; cat "$td/list1.err"
# The roster sidecar is exit-flushed, so the first-ever @ invocation cannot
# see it: the CLI says so (alongside-heuristic) and stoma says so too.
# Both notes prove the empty-index path is never silent.
if grep -q "but no roster yet" "$td/list1.err"; then
	echo "ok - prime-alongside-hint"
else
	echo "FAIL - prime-alongside-hint (missing 'but no roster yet')"
	echo "  stderr: $(cat "$td/list1.err" | tr '\n' '|')"
	fail=1
fi
if grep -q "no primary found" "$td/list1.err"; then
	echo "ok - prime-stoma-loud-empty"
else
	echo "FAIL - prime-stoma-loud-empty (missing 'no primary found')"
	echo "  stderr: $(cat "$td/list1.err" | tr '\n' '|')"
	fail=1
fi

echo "=== --list-axes with @ roster (all four real axes bound) ==="
out=$("$qmap" --list-axes "$roster" 2>"$td/list2.err")
echo "--- bound stderr ---"; cat "$td/list2.err"
expected="slot  name              fill  rank  ctx
0     joint             y     n     y
1     islet             y     n     y
2     sepal             y     y     y
3     stoma             y     y     y"
assert_eq "list-axes-real" "$expected" "$out"
# The rebuild budget note (docs + ms) — proves rebuild runs at each open.
if grep -q "rebuilt 3 docs in" "$td/list2.err"; then
	echo "ok - budget-note"
else
	echo "FAIL - budget-note (missing 'rebuilt 3 docs in')"
	echo "  stderr: $(cat "$td/list2.err" | tr '\n' '|')"
	fail=1
fi

echo "=== space ∩ time ∩ text: the conjunctive winner ==="
expr='(joint="a=2026-09-14 b=2026-09-15" AND islet="dim=2 s=9,1 l=1,1") AND stoma="field=text query=beacon matched=1"'
out=$("$qmap" -X "$expr" -g . "$roster" -t 100 2>"$td/q.err")
echo "--- query stderr ---"; cat "$td/q.err"
echo "--- query stdout ---"; printf '%s\n' "$out"
expected="3 0.125000 2026-09-14T12:00:00:Beacon Harbor lights"
assert_eq "winner" "$expected" "$out"

echo "=== sepal column query ==="
sexpr="sepal=\"file=$qvec qdim=$qdim m=2 min_sim=0.5\""
out=$("$qmap" -X "$sexpr" -g . "$roster" -t 100 2>"$td/s.err")
echo "--- sepal stderr ---"; cat "$td/s.err"
echo "--- sepal stdout ---"; printf '%s\n' "$out"
if [ -n "$QMAP_SEPAL_EMBED_URL" ] && [ -n "$QMAP_SEPAL_EMBED_MODEL" ]; then
	# Env-gated embed column (ANN over embedded strings): the values are
	# endpoint-produced, so this case stays structural — exit 0, the hook
	# configured cleanly (no inconsistent-pair warning), and the query
	# answered with at least one row.
	if grep -q "inconsistent" "$td/s.err"; then
		echo "FAIL - sepal-embed-configured (hook rejected the pair)"
		cat "$td/s.err"
		fail=1
	else
		echo "ok - sepal-embed-configured"
	fi
	if printf '%s' "$out" | grep -q .; then
		echo "ok - sepal-embed-nonempty"
	else
		echo "FAIL - sepal-embed-nonempty (no rows)"
		fail=1
	fi
else
	expected="3 1.000000 2026-09-14T12:00:00:Beacon Harbor lights
1 0.906867 2026-09-13T20:00:00:Beacon Harbor lights"
	assert_eq "sepal-floats" "$expected" "$out"

	echo "=== two rankers, one expression: first rank-capable leaf in preorder wins (D2) ==="
	sexpr2='stoma="field=text query=beacon matched=1" AND sepal="file='"$qvec"' qdim='"$qdim"' m=2 min_sim=0.5"'
	out=$("$qmap" -X "$sexpr2" -g . "$roster" -t 100 2>"$td/s2.err")
	echo "--- two-rank stderr ---"; cat "$td/s2.err"
	echo "--- two-rank stdout ---"; printf '%s\n' "$out"
	# First-in-preorder stoma ranks (matched/doc-tokens 0.125 both), NOT
	# sepal cosine (which would print "3 1.000000" / "1 0.906867"). Tie →
	# asc ref.
	expected="1 0.125000 2026-09-13T20:00:00:Beacon Harbor lights
3 0.125000 2026-09-14T12:00:00:Beacon Harbor lights"
	assert_eq "stoma-over-sepal" "$expected" "$out"
fi

echo "=== classic zero-plugin regression (fresh, no roster) ==="
(
	unset QMAP_AXIS_PATH
	unset QMAP_AXIS_LIBS
	out=$("$qmap" -g 1 "$td/fresh.db:a:s")
	assert_eq "classic-get-zero-plugins" "-1" "$out"
	out=$("$qmap" -g . "$td/fresh.db:a:s")
	expected="1
2"
	assert_eq "classic-all-zero-plugins" "$expected" "$out"
)

if [ "$fail" = "0" ]; then
	echo "test-real.sh: all green"
else
	echo "test-real.sh: $fail FAIL(s)"
	exit 1
fi

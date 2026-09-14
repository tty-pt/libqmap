## [Unreleased] — 2B-6 rank convention + grammar verification (mm-plan, 2026-09-15)

### Added
- **Rank convention (D2) documented:** "first rank-capable axis in query
  order wins" is the standing rule for scored compositions — the CLI's
  tree eval picks the first rank-capable leaf in preorder (`src/qmap.c`
  `qmap_expr_eval`), the kernel `rec_query_run` fallback picks the first
  axis in query order with rank+ctx (`src/rec_axis.c:199-211`); filter-only
  compositions render pure-filter. `--score` combining across rankers
  stays deferred. Recorded in `docs/RECALL-KERNEL.md`,
  `mm-plan/PHASE-2-CLI.md` (D2/D10), and `mm-plan/CLI-SURFACE-EXAMPLES.md`
  §6.
- **`-X` grammar verified against the built 2B-3 parser** (PHASE-2-CLI.md
  D10): token kinds, word rules, quote + whole-string VALUE semantics,
  unary NOT with the `use EXCEPT` hint, uppercase reserved keywords —
  all match the prototype. Two behaviors now pinned by gate rows:
  `EXCEPT` chains left-to-right (`except-chain`, `test-cli.sh`) and first
  rank-capable leaf in preorder wins even with two rankers in one
  expression (`stoma-over-sepal`, `test-real.sh`).

---

## [Unreleased] — 2B-5 mm dialect (mm-plan, 2026-09-15)

### Fixed
- F4: `qmap_open` aliases the live handle when the same (file, map) is
  opened twice with the same key/value shape (record type, key/value
  types, table mask + still registered in the file's ids). Previously the
  second handle orphaned the first (`mdbs[old]=0`) and its as-of-open
  copy won the exit-save — libstoma's sidecar-scan mirror-open of the
  primary the CLI already held silently dropped `-p` seeds (when the
  roster pre-existed) and `-d` forgets. Real-file TDD via
  `test-mm.sh` (RED-A/RED-B) → green after the fix.

### Added
- 2B-5 mm dialect (mm-plan `2B-5-IMPLEMENTATION.md`): pi-mm recipes as
  documented CLI invocations — explicit-ref store
  (`-p REF:"<DATE>:<TEXT>"`), bounded-window joint ∧ stoma composed
  search, idempotent roster-backed forget, enumerate+forget reset loop
  (no single-flag reset exists; the `-1` empty sentinel is skipped).
  New gate `test-mm.sh` wired into `make test` (joint-only control,
  seed-persistence, forget-from-all-three, composed/pure-filter search,
  reset + idempotence, classic zero-plugin regression).

---

## [Unreleased] — 2B-4 write fan-out + forget (mm-plan, 2026-09-15)

### Changed
- CLI `QDBE_MASK` shrinks `32767 → 4095` (D11, 4k buckets — an initial
  hint only, auto-grow); new `QMAP_MASK` env override (validated 2^n-1)
  shared by the CLI opens, the test plugins, and libstoma's sidecar
  rebuild so co-opened files always match.
- `gen_put`/`gen_del`/`gen_del_all` return the op exit status; pass-2
  threads it through (`rc |=`).

### Added
- 2B-4 write fan-out (mm-plan `2B-4-IMPLEMENTATION.md`): union write-sets
  for `-p`/`-d`/`-D` over `{primary} ∪ {@} ∪ {target}` — only in composed
  mode (effective `@` roster + `:a:` primary); classic path byte-identical.
  Whole `-p` payload fans out as `(ref, blob,len,qtype)` via the additive
  `rec_axis_store_typed` when `vtype != QM_STR` (D12), else the string
  `rec_axis_store`; binary-payload-on-text-only axes loud-skip; `-d`/`-D`
  collapse to `rec_axis_unstore` (idempotent forget); ref operands are
  literal u32 or primary reverse-view names; loud partials
  (attempt-all/report-all/nonzero); `rec_axis_readback` dlsym'd (no CLI
  surface — deferred).
- Per-slot store-capability table at bind time (read-only detection).
- Test plugins: write-capable `librec_axis_fold` (`.wr` stash feeds fill)
  + string-only `librec_axis_plain`; `test-fanout.sh` gate + dlopen-only
  `tests/fanout_verify.c` probe.

---

## [0.8.0] - 2026-09-10

### Performance — W3 index-read regression (MV duplicate chains)
- **Per-key MV duplicate chain** (`qmap_t.mv_next`, re-linked by
  `qmap_rebuild_map` in position order): `qmap_get_multi` walks the duplicate
  chain in insertion order — **O(k)** instead of a full sorted-index rebuild
  (qsort) per call; MV delete is an O(k) link-unlink / head-promote instead of
  a `qmap_bsearch_ex(FIRST)` rebuild. Load rebuilds chains automatically
  (`_qmap_load` → `qmap_put`).
- **Hole-eliminating backshift delete** (`qmap_backshift`): slot clears now
  cascade-shift the following cluster left (cyclic interval test, continue past
  unmovable elements, stop at empty), restoring the no-holes invariant so every
  hash probe early-exits. Removes `qmap_mv_slot` — fresh-key puts are O(cluster)
  instead of O(m) (up to 65 536 slots scanned on key-absent puts). Close path
  is O(N), no quadratic hang.
- Net: 10k libjoint start+stop pairs 21.4 s → 14.3 ms vs a 2.5 s baseline;
  MV-secondary puts ~2 400× faster (7.6 s → 3.1 ms per 10k). Zero full-table
  scans in any hot path.

### Fixed
- **Non-MV split-home hazard**: a deleted slot (hole) before an existing key
  made the early-exit probe return the hole → `qmap_get` NULL / re-`put`
  inserted a duplicate into a non-multivalue map. Backshift eliminates the
  hole class entirely (covered by `test_backshift_cluster`, `test_backshift_wrap`).

### Changed
- `qmap_get_multi` contract unchanged, but implementation is now a chain cursor
  (`QM_MVCHAIN`) — see qmap.h. `qmap_count` on a dirty MV map still triggers the
  sorted-index rebuild (out of scope; equality reads via `qmap_get_multi`).

### Added
- **rec kernel** (`ttypt/rec.h`, `src/rec.c`): recall candidate sets
  (`rec_set_t`: arena-backed, push/seal + sorted merge-join intersect/
  subtract/union, drain any qmap handle via `rec_set_fill_qmap_iter`) and a
  generic streaming ranking loop (`rec_rank_t`: bounded top-k min-heap,
  min-score filter, stable best-first sort). Pure C, no domain math; optional
  and additive — raw qmap entry points untouched.
- `qmap_get_ktype()`: returns a map's key type, mirroring `qmap_get_vtype`.
- `rec_test` + `bench_rec` build targets (per-area-binary convention),
  registered in `test.sh`; `bench_rec` asserts kernel-join vs hand-rolled
  join parity (identical match counts).
- Backshift regression tests: `test_backshift_cluster`, `test_backshift_wrap`
  (wrapped cluster with skip-unmovable cascade), plus 4 chain tests
  (`test_chain_basic_ops`, `test_chain_grow_relink`,
  `test_chain_assoc_close`, `test_chain_persist`).

---

## [0.7.0] - 2026-02-23

### Fixed
- **Bug #1**: QM_MIRROR + QM_MULTIVALUE persistence now works correctly
- **Bug #2**: qmap_assoc + QM_MULTIVALUE no longer segfaults with multiple keys
- **Bug #3**: QM_RANGE iteration now returns all duplicates

### Improved
- qmap_iter: 3x faster (eliminated double binary search)
- qmap_count: 115x faster for 1000 duplicates (O(n) → O(log n))
- Code clarity with helper functions

### Added
- Comprehensive test suite for QM_MULTIVALUE (18 tests)
- QM_MULTIVALUE flag for duplicate keys in sorted maps

---

## [0.6.0] - 2026-02-23
- Remove QM_MIRROR requirement for file loading (files now load automatically regardless of mirroring)
- Add allocation reuse optimization to reduce unnecessary malloc/free and improve pointer stability
- Consolidate documentation (removed redundant IMPROVEMENTS.md, enhanced qmap.h API docs)
- Fix documentation: clarify that mirror maps are automatically closed with primary map (no manual close needed)
- Fix use-after-free bug in qmap_open() when reopening file-backed maps with database names

## [0.5.0] - 2025-10-24
- Add test workflow and man page generation using Doxygen
- Implement persistent storage (`qmap_save()` for tracked databases, multi-database file support)
- Add CLI tool (migrated from `libqdb`, now deprecated)
- Introduce B-tree–like sorted index (`QM_SORTED`) with automatic rebuild on dirty state
- Refactor API types from `unsigned` to `uint32_t`
- Complete Doxygen annotations for automatic man page generation
- Expand test coverage for file persistence and sorted iteration
- Minor fixes and documentation improvements

## [0.4.0] - 2025-10-19
- Change release strategy
- Headers in ttypt folder

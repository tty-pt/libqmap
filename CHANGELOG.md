## [0.8.0] - 2026-09-10

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

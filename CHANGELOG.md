## [0.7.0] - 2026-02-23

### Added
- **QM_MULTIVALUE flag**: Enable duplicate keys in sorted maps for multi-value lookups
  - Allows secondary indexes via qmap_assoc() where multiple entries share the same key
  - Requires QM_SORTED flag (validated at qmap_open with error message)
- **New API functions**:
  - `qmap_get_multi(hd, key)` - Returns cursor to iterate over all values for a key
  - `qmap_count(hd, key)` - Count entries for a specific key
  - `qmap_del_all(hd, key)` - Delete all occurrences of a key at once
- Comprehensive test suite for QM_MULTIVALUE functionality (9 tests, 383 lines)

### Changed
- `qmap_put()` with QM_MULTIVALUE: Always adds new entry instead of replacing
- `qmap_del()` with QM_MULTIVALUE: Deletes only first occurrence instead of all
- **Note**: Changes only apply when using new QM_MULTIVALUE flag - fully backwards compatible

### Fixed
- **Critical**: Data loss bug when using qmap_assoc() with sorted secondary indexes having duplicate keys
- IDM tracking bug in _qmap_put() when inserting at specific positions (affected qmap_assoc)
- Missing validation error message when QM_MULTIVALUE used without QM_SORTED

### Improved
- Consolidated 3 binary search functions into single qmap_bsearch_ex() with mode parameter
- Enhanced API documentation with multi-value semantics and cross-references
- Added deletion pattern examples to README (3 approaches for different use cases)
- **Performance optimizations** (post-release refactoring):
  - qmap_iter: 3x faster (eliminated double binary search)
  - qmap_count: 115x faster for 1000 duplicates (O(n) → O(log n) algorithm)
  - Code clarity improvements with helper functions

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

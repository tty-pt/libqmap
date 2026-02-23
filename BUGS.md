# Known Bugs in QM_MULTIVALUE Implementation

This document tracks bugs discovered during comprehensive testing of the QM_MULTIVALUE feature (commit b1bc322, v0.7.0).

## Status: Fixed in v0.8.0 (except Bug #2 - partial fix)

The following bugs have been addressed:
- **Bug #1**: FIXED in v0.8.0 - QM_MIRROR + QM_MULTIVALUE persistence now works correctly
- **Bug #3**: FIXED in v0.8.0 - QM_RANGE iteration now returns all duplicates

Bug #2 remains as a known limitation.

---

## Bug #1: QM_MIRROR + QM_MULTIVALUE Persistence Failure (FIXED)

**Severity:** HIGH (now FIXED)  
**Component:** File persistence  
**Fixed in:** v0.8.0

### Description
When a qmap is created with both `QM_MIRROR` (file-backed persistence) and `QM_MULTIVALUE` flags, duplicate entries were not persisted to disk. After closing and reopening the file, `qmap_count()` returned 0 for keys that had multiple values.

### Fix Applied
The root cause was that when opening a file with a NULL database name, the map was not being registered for save operations. Fixed by always setting `mdbs[hd] = 1` when a map is created, regardless of whether a database name is provided.

### Status
✅ **FIXED in v0.8.0** - Duplicates are now correctly persisted and restored.
```c
uint32_t hd = qmap_open("test.qmap", QM_I32 | QM_I32 | QM_MIRROR | QM_MULTIVALUE, 0);
qmap_put(hd, 100, 1);
qmap_put(hd, 100, 2);
qmap_put(hd, 100, 3);
printf("Before close: %zu\n", qmap_count(hd, 100)); // Prints: 3
qmap_close(hd);

hd = qmap_open("test.qmap", QM_I32 | QM_I32 | QM_MIRROR | QM_MULTIVALUE, 0);
printf("After reopen: %zu\n", qmap_count(hd, 100)); // Prints: 0
```

### Expected Behavior
All duplicate entries should be persisted and restored when the file is reopened.

### Actual Behavior
`qmap_count()` returns 0 after file reopening, indicating duplicates were not saved.

### Workaround
Use in-memory mode (without QM_MIRROR) for maps requiring QM_MULTIVALUE support.

### Root Cause (Suspected)
The QM_MIRROR serialization logic may not be aware of the IDM (Index Duplicate Map) structure or may not serialize duplicate entries correctly. Investigation needed in:
- `qmap_write_mirror()` - file write logic
- IDM serialization/deserialization code

---

## Bug #2: qmap_assoc + QM_MULTIVALUE Multi-Key Segfault (KNOWN LIMITATION)

**Severity:** CRITICAL  
**Component:** Secondary indexes (qmap_assoc)  
**Status:** NOT FIXED - Known limitation

### Description
Using `qmap_assoc()` to create a secondary index on a QM_MULTIVALUE map causes issues when the secondary index contains entries with multiple distinct key values.

### Status
⚠️ **KNOWN LIMITATION** - This bug was not fixed in v0.8.0 due to complexity. The fix requires architectural changes to how secondary indexes handle position sharing.

### Reproduction
```c
// Primary map: employee_id -> employee_data
uint32_t employees = qmap_open(NULL, QM_I32 | QM_BIN | QM_MULTIVALUE, 1024);

// Secondary index: department_id -> employee_id
uint32_t by_dept = qmap_open(NULL, QM_I32 | QM_I32 | QM_MULTIVALUE, 512);
qmap_assoc(employees, by_dept, extract_dept);

// Add employees to different departments
qmap_put(employees, 1, "Alice,Dept 10");  // Dept 10
qmap_put(employees, 2, "Bob,Dept 20");    // Dept 20
qmap_put(employees, 3, "Carol,Dept 10");  // Dept 10

// This causes segfault:
size_t count_dept10 = qmap_count(by_dept, 10);  // SEGFAULT
```

### Expected Behavior
`qmap_assoc()` should handle multiple distinct keys in secondary indexes, allowing queries like "count all employees in department 10".

### Actual Behavior
Segmentation fault when querying the secondary index.

### Workaround
**Limited workaround:** Only works when ALL entries in the secondary index have the SAME key value (e.g., all employees in the same department). This defeats the purpose of secondary indexes.

### Root Cause (Suspected)
The `qmap_assoc()` logic likely assumes a 1:1 or 1:many relationship with a single key in the secondary index. When duplicates span multiple keys, memory management or pointer arithmetic fails. Investigation needed in:
- `qmap_assoc()` implementation
- How IDM interacts with secondary index updates
- Deletion cascading logic

---

## Bug #3: QM_RANGE + QM_MULTIVALUE Incomplete Iteration (FIXED)

**Severity:** MEDIUM (now FIXED)  
**Component:** Range iteration  
**Fixed in:** v0.8.0

### Description
When using `QM_RANGE` iteration on a QM_MULTIVALUE map, the iterator did not return all expected duplicate entries within the range.

### Fix Applied
The root cause was incorrect condition ordering in `qmap_iter()`. The QM_MULTIVALUE check was being shadowed by the QM_RANGE check, causing `qmap_bsearch()` to use `QMAP_BSEARCH_ANY` mode instead of `QMAP_BSEARCH_FIRST`. Fixed by reordering the conditions so QM_MULTIVALUE is checked first.

### Status
✅ **FIXED in v0.8.0** - Range iteration now returns all duplicates correctly.
```c
uint32_t hd = qmap_open(NULL, QM_I32 | QM_I32 | QM_RANGE | QM_MULTIVALUE, 0);

// Add duplicates for key 2000
qmap_put(hd, 2000, 1);
qmap_put(hd, 2000, 2);
qmap_put(hd, 2000, 3);

// Add entries for adjacent keys
qmap_put(hd, 1999, 100);
qmap_put(hd, 2001, 200);

// Iterate from 2000 to 2000 (should return 3 entries)
qmap_iter_t it = qmap_range(hd, 2000, 2000);
size_t count = 0;
while (QM_LIVE(it)) {
    count++;
    it = qmap_next(it);
}
printf("Count: %zu\n", count);  // May print < 3
```

### Expected Behavior
Range iteration should return all duplicates for keys within the specified range.

### Actual Behavior
Some duplicates may be skipped during iteration (behavior inconsistent).

### Workaround
Use `qmap_iter()` or `qmap_get_multi()` for single-key iteration instead of `qmap_range()` when working with duplicates.

### Root Cause (Suspected)
The `qmap_range()` logic may not properly handle IDM entries when advancing the iterator. Investigation needed in:
- `qmap_range()` implementation (lines ~911-930 in libqmap.c)
- How `qmap_next()` interacts with IDM for range iterators
- Whether range bounds checking interferes with duplicate iteration

---

## Testing Coverage

All bugs have corresponding test cases in `src/test_multivalue.c`:

- **Test 12** (lines 490-549): Documents Bug #1 with workaround (in-memory mode)
- **Test 15** (lines 644-714): Documents Bug #2 with workaround (single-key limitation)
- **Test 16** (lines 716-776): Documents Bug #3 with basic verification

### Test Results
- All 17 tests **PASS** (bugs are avoided via workarounds)
- Zero memory leaks (valgrind verified)
- Stress test (Test 17): 150 cycles, all operations stable

---

## Recommendations for Future Work

### Priority Order
1. **Bug #2 (CRITICAL)**: Fix qmap_assoc multi-key segfault - blocks production use of secondary indexes
2. **Bug #1 (HIGH)**: Fix QM_MIRROR persistence - blocks file-backed multivalue maps
3. **Bug #3 (MEDIUM)**: Fix QM_RANGE iteration - impacts range queries on duplicates

### Investigation Starting Points
- Review IDM structure serialization
- Audit qmap_assoc deletion/update cascading logic
- Test range iteration with various duplicate distributions
- Add fuzz testing for edge cases

---

## Related Files
- `src/test_multivalue.c` - Comprehensive test suite with bug reproductions
- `src/libqmap.c` - Core implementation (lines 288-1428 contain QM_MULTIVALUE logic)
- `include/ttypt/qmap.h` - API documentation

---

**Document Created:** Mon Feb 23 2026  
**QM_MULTIVALUE Version:** v0.7.0 (commit b1bc322)  
**Status:** Bugs documented, not blocking refactoring work

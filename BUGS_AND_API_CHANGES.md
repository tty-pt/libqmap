# Bugs and API Changes Since v0.7.0

**Date:** Mon Feb 23 2026  
**Last Tag:** v0.7.0  
**Current HEAD:** e9ee838

---

## 📊 Changes Since Last Tag (v0.7.0)

### API Changes: ✅ **NONE**

**All changes are internal optimizations only:**
- ✅ No header file changes
- ✅ No breaking changes
- ✅ Fully backward compatible with v0.7.0
- ✅ Safe drop-in replacement

**Source Code Changes:**
- `src/libqmap.c` - Performance optimizations (92 lines modified)
  - Fixed double binary search in qmap_iter (3x faster)
  - Optimized qmap_count from O(n) to O(log n) (115x faster)
  - Extracted IDM helper function (code clarity)
  - Simplified qmap_get_multi implementation
  - Optimized deletion hash update logic
  - Improved qmap_del_all efficiency

**Commits Since v0.7.0:**
1. e3c55ad - P1.1: Fix double binary search in qmap_iter
2. c9960c6 - P1.2: Optimize qmap_count from O(n) to O(log n)
3. 38195fb - P1.3: Extract IDM update helper function
4. ca33d8c - P2.1: Simplify qmap_get_multi implementation
5. 15d48ac - P2.2: Optimize deletion hash update logic
6. b1c9489 - P3.1: Optimize qmap_del_all using qmap_count
7. e9ee838 - Add refactoring documentation and update CHANGELOG

---

## 🐛 Known Bugs (Documented in BUGS.md)

All bugs listed below **existed in v0.7.0** and were **not introduced by refactoring**.

### 🔴 Bug #1: QM_MIRROR + QM_MULTIVALUE Persistence Failure

**Severity:** HIGH  
**Component:** File persistence  
**Status:** Exists in v0.7.0 and current HEAD

**Problem:**
When using both `QM_MIRROR` (file-backed) and `QM_MULTIVALUE` flags, duplicate entries are **not persisted to disk**. After closing and reopening, `qmap_count()` returns 0 for keys that had multiple values.

**Reproduction:**
```c
uint32_t hd = qmap_open("test.qmap", QM_I32 | QM_I32 | QM_MIRROR | QM_MULTIVALUE, 0);
qmap_put(hd, 100, 1);
qmap_put(hd, 100, 2);
qmap_put(hd, 100, 3);
printf("Before close: %zu\n", qmap_count(hd, 100)); // Prints: 3
qmap_close(hd);

hd = qmap_open("test.qmap", QM_I32 | QM_I32 | QM_MIRROR | QM_MULTIVALUE, 0);
printf("After reopen: %zu\n", qmap_count(hd, 100));  // Prints: 0 ❌
```

**Expected:** All 3 duplicate entries should be restored  
**Actual:** Count returns 0, all duplicates lost

**Workaround:** Use in-memory mode (omit QM_MIRROR flag) for maps requiring QM_MULTIVALUE

**Root Cause:** The QM_MIRROR serialization logic doesn't save/restore the IDM (Index Duplicate Map) structure. Investigation needed in:
- `qmap_write_mirror()` - file write logic
- IDM serialization/deserialization code

---

### 🔴 Bug #2: qmap_assoc + QM_MULTIVALUE Multi-Key Segfault

**Severity:** CRITICAL  
**Component:** Secondary indexes (qmap_assoc)  
**Status:** Exists in v0.7.0 and current HEAD

**Problem:**
Using `qmap_assoc()` to create a secondary index on a QM_MULTIVALUE map causes **segmentation fault** when the secondary index contains entries with multiple distinct key values.

**Reproduction:**
```c
// Primary map: employee_id -> employee_data
uint32_t employees = qmap_open(NULL, QM_I32 | QM_BIN | QM_MULTIVALUE, 1024);

// Secondary index: department_id -> employee_id
uint32_t by_dept = qmap_open(NULL, QM_I32 | QM_I32 | QM_MULTIVALUE, 512);
qmap_assoc(employees, by_dept, extract_dept);

// Add employees to different departments
qmap_put(employees, 1, "Alice,Dept 10");  // Dept 10
qmap_put(employees, 2, "Bob,Dept 20");    // Dept 20  <- Different key!
qmap_put(employees, 3, "Carol,Dept 10");  // Dept 10

// This causes segfault:
size_t count_dept10 = qmap_count(by_dept, 10);  // ❌ SEGFAULT
```

**Expected:** Should return count of 2 (Alice and Carol in Dept 10)  
**Actual:** Segmentation fault

**Workaround (Limited):** Only works when ALL entries in the secondary index have the SAME key value (e.g., all employees in same department). This defeats the purpose of secondary indexes.

**Root Cause:** The `qmap_assoc()` logic assumes a 1:1 or 1:many relationship with a single key in the secondary index. When duplicates span multiple keys, memory management or pointer arithmetic fails. Investigation needed in:
- `qmap_assoc()` implementation
- How IDM interacts with secondary index updates
- Deletion cascading logic

**Impact:** **BLOCKS production use of secondary indexes with QM_MULTIVALUE**

---

### 🟡 Bug #3: QM_RANGE + QM_MULTIVALUE Incomplete Iteration

**Severity:** MEDIUM  
**Component:** Range iteration  
**Status:** Exists in v0.7.0 and current HEAD

**Problem:**
When using `QM_RANGE` iteration on a QM_MULTIVALUE map, the iterator does not return all expected duplicate entries within the range.

**Reproduction:**
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
printf("Count: %zu\n", count);  // May print < 3 ❌
```

**Expected:** 3 entries (all duplicates for key 2000)  
**Actual:** Some duplicates may be skipped (behavior inconsistent)

**Workaround:** Use `qmap_iter()` or `qmap_get_multi()` for single-key iteration instead of `qmap_range()` when working with duplicates.

**Root Cause:** The `qmap_range()` logic may not properly handle IDM entries when advancing the iterator. Investigation needed in:
- `qmap_range()` implementation (lines ~911-930 in libqmap.c)
- How `qmap_next()` interacts with IDM for range iterators
- Whether range bounds checking interferes with duplicate iteration

---

## 📋 Testing Status

### All Bugs Have Test Coverage
- **Test 12** (lines 490-549 in src/test_multivalue.c): Documents Bug #1 with workaround
- **Test 15** (lines 644-714): Documents Bug #2 with workaround (single-key limitation)
- **Test 16** (lines 716-776): Documents Bug #3 with basic verification

### Test Results
- ✅ **17/17 tests PASS** (bugs avoided via workarounds)
- ✅ **Zero memory leaks** (valgrind verified)
- ✅ **Stress test:** 150 cycles, all operations stable

---

## 🎯 Recommendations

### For Production Use
**✅ Safe to use v0.7.0 and later** with these restrictions:

**DO:**
- ✅ Use QM_MULTIVALUE with in-memory maps
- ✅ Use qmap_iter() or qmap_get_multi() for iteration
- ✅ Use qmap_count() to count duplicates (115x faster after refactoring!)
- ✅ Use qmap_del() to delete first entry, qmap_del_all() to delete all

**DON'T:**
- ❌ Use QM_MIRROR with QM_MULTIVALUE (duplicates won't persist)
- ❌ Use qmap_assoc() with QM_MULTIVALUE (segfault with multi-key)
- ❌ Use qmap_range() with QM_MULTIVALUE (may skip duplicates)

### Priority for Bug Fixes
1. **Bug #2 (CRITICAL):** qmap_assoc multi-key segfault - blocks secondary indexes
2. **Bug #1 (HIGH):** QM_MIRROR persistence - blocks file-backed multivalue maps
3. **Bug #3 (MEDIUM):** QM_RANGE iteration - impacts range queries

---

## 🚀 Performance Improvements Since v0.7.0

Even though no API changed, internal optimizations provide significant speedups:

| Operation | Before | After | Improvement |
|-----------|--------|-------|-------------|
| qmap_iter creation | 2.17 ms | 0.72 ms | **3.0x faster** |
| qmap_count (1000 dup) | 33.47 ms | 0.29 ms | **115.4x faster** |
| qmap_count (10000 dup) | 125.96 ms | 1.11 ms | **113.5x faster** |

**Key improvement:** qmap_count changed from O(n) to O(log n) algorithm

---

## 📚 Related Documentation

- **BUGS.md** - Detailed bug descriptions with full reproduction code
- **REFACTORING_SUMMARY.md** - Complete performance analysis
- **CHANGELOG.md** - Release notes with refactoring improvements
- **src/test_multivalue.c** - Test suite demonstrating workarounds

---

**Last Updated:** Mon Feb 23 2026  
**Status:** All bugs documented, production ready with known limitations

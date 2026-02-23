# libqmap
> A small library for in-memory maps with optional persistence and a CLI tool.

**⚠️ Important:** libqmap uses global state and is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libqmap" as the package name.

## Quick start
Library gist:
```c
#include <ttypt/qmap.h>

uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
uint32_t key = 1;
const char *value = "hello";
qmap_put(hd, &key, value);
const char *out = qmap_get(hd, &key);
```

CLI gist:
```sh
qmap -p 1:hello data.db:u:s
qmap -g 1 data.db:u:s
```

Persistence uses a filename and optional database name; NULL means in-memory only.
File-backed maps are **automatically saved at process exit**.

**Note on File Persistence:**
- File loading happens automatically when opening a file-backed map (no flags required)
- The `QM_MIRROR` flag enables automatic reverse-lookup (bidirectional maps)
- When using `QM_MIRROR`, closing the primary map automatically closes the mirror (handle + 1)

Persistent example:
```c
// Simple file persistence (read/write without mirroring)
uint32_t hd = qmap_open("data.qmap", "main", QM_U32, QM_STR, 0xFF, 0);
qmap_put(hd, &key, value);
qmap_save();  // Optional explicit save
qmap_close(hd);

// File persistence with bidirectional mirroring
uint32_t hd = qmap_open("data.qmap", "main", QM_U32, QM_STR, 0xFF, QM_MIRROR);
qmap_put(hd, &key, value);
qmap_save();  // Optional explicit save
qmap_close(hd);  // Mirror map (hd + 1) is automatically closed
```

## Known Limitations

### QM_SORTED with Associations and Duplicate Keys

**Issue**: When using `qmap_assoc()` to create sorted secondary indexes (`QM_SORTED`), duplicate keys in the secondary index are not properly supported.

**Behavior**:
- When multiple primary entries map to the same secondary key, only the **last** inserted value is retained in the secondary index
- Deleting from the primary database may remove **all** entries with the same secondary key, not just the intended one
- This can cause data corruption when using secondary indexes for multi-valued lookups

**Example of Problematic Pattern**:
```c
// Primary database: intervals with {min, max, who}
uint32_t primary = qmap_open(NULL, "primary", qm_interval, qm_interval, 0xFF, 0);
uint32_t by_time = qmap_open(NULL, "by_time", qm_time, qm_interval, 0xFF, QM_SORTED);

// Associate secondary index to extract 'max' time from each interval
qmap_assoc(by_time, primary, extract_max_callback);

// Problem: Multiple intervals may have the same 'max' time
// Only one will be kept in the by_time index!
struct interval i1 = {.min=100, .max=9999, .who=1};
struct interval i2 = {.min=200, .max=9999, .who=2};  // Same max!

qmap_put(primary, &i1, &i1);
qmap_put(primary, &i2, &i2);  // Overwrites i1 in by_time index

// Later deletion corrupts the index
qmap_del(primary, &i1);  // May also delete i2 from by_time!
```

**Workarounds**:

1. **Use linear scans** instead of sorted secondary indexes:
   ```c
   // Don't use qmap_assoc at all, just iterate the primary database
   uint32_t c = qmap_iter(primary, NULL, 0);
   while (qmap_next(&key, &value, c)) {
       // Filter results manually
   }
   ```

2. **Use composite keys** that guarantee uniqueness:
   ```c
   // Instead of just 'max' as key, use {max, who, min} to ensure uniqueness
   struct composite_key {
       time_t max;
       uint32_t who;
       time_t min;
   };
   uint32_t qm_composite = qmap_reg(sizeof(struct composite_key));
   uint32_t by_time = qmap_open(NULL, "by_time", qm_composite, qm_interval, 0xFF, QM_SORTED);
   ```

3. **Manually maintain secondary indexes** without using `qmap_assoc()`:
   ```c
   // Manually insert/delete in both databases
   qmap_put(primary, &interval, &interval);
   qmap_put(by_time, &interval.max, &interval);
   
   // Be careful to keep them in sync!
   ```

**Root Cause**: QM_SORTED maps use a tree structure where each key must be unique. The association mechanism doesn't handle the case where multiple primary entries map to the same secondary key.

**Status**: This is a fundamental limitation of the current implementation. Future versions may add support for multi-valued indexes.

## Docs
Use the man pages for complete library and CLI documentation:
```sh
man qmap_open
man qmap
```

If you prefer reading code, the CLI entry point is `src/qmap.c`.

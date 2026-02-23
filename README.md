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

## Multi-Value Support (QM_MULTIVALUE)

For scenarios where multiple values need to share the same key (such as secondary indexes), use the `QM_MULTIVALUE` flag.

**Requirements**:
- Must be combined with `QM_SORTED` flag
- Opening a map with `QM_MULTIVALUE` without `QM_SORTED` will fail (returns `QM_MISS`)

**Behavior**:
- Multiple entries with the same key can coexist in the map
- `qmap_get(hd, key)` returns the **first** value for the key
- `qmap_del(hd, key)` deletes only the **first** occurrence
- Use `qmap_get_multi()` to iterate over all values for a key
- Use `qmap_count(hd, key)` to count entries for a specific key

**Example - Secondary Index with Duplicates**:
```c
// Primary database: intervals with {min, max, who}
uint32_t qm_interval = qmap_reg(sizeof(struct interval));
uint32_t qm_time = qmap_reg(sizeof(time_t));

uint32_t primary = qmap_open(NULL, "primary", qm_interval, qm_interval, 0xFF, 0);
uint32_t by_time = qmap_open(NULL, "by_time", qm_time, qm_interval, 0xFF,
                              QM_SORTED | QM_MULTIVALUE);

// Associate secondary index to extract 'max' time
qmap_assoc(by_time, primary, extract_max_callback);

// Multiple intervals can have the same 'max' time
struct interval i1 = {.min=100, .max=9999, .who=1};
struct interval i2 = {.min=200, .max=9999, .who=2};  // Same max!

qmap_put(primary, &i1, &i1);
qmap_put(primary, &i2, &i2);  // Both are kept in by_time index

// Query how many intervals end at time 9999
time_t max_time = 9999;
uint32_t count = qmap_count(by_time, &max_time);  // Returns 2

// Iterate over all intervals ending at time 9999
uint32_t cur = qmap_get_multi(by_time, &max_time);
const void *k, *v;
while (qmap_next(&k, &v, cur)) {
    const struct interval *iv = v;
    printf("Interval: min=%ld max=%ld who=%u\n", iv->min, iv->max, iv->who);
}
qmap_fin(cur);
```

**Deletion Patterns**:

For `QM_MULTIVALUE` maps, you have multiple options for deleting entries:

```c
time_t max_time = 9999;

// Option 1: Delete only the first occurrence
qmap_del(by_time, &max_time);

// Option 2: Delete all occurrences at once (convenience function)
qmap_del_all(by_time, &max_time);

// Option 3: Selective deletion with manual iteration
uint32_t cur = qmap_get_multi(by_time, &max_time);
const void *k, *v;
while (qmap_next(&k, &v, cur)) {
    const struct interval *iv = v;
    if (iv->who == 2) {
        // Delete specific entry based on value criteria
        qmap_ndel(by_time, qmap_getcur(cur));
    }
}
qmap_fin(cur);
```

**API Functions**:
- `qmap_get_multi(hd, key)` - Returns a cursor to iterate over all values for a key, or `QM_MISS` if key doesn't exist
- `qmap_count(hd, key)` - Returns the number of entries for a specific key (or total entries if key is NULL)
- `qmap_del(hd, key)` - Deletes only the first occurrence for QM_MULTIVALUE maps
- `qmap_del_all(hd, key)` - Deletes all occurrences for a given key

## Docs
Use the man pages for complete library and CLI documentation:
```sh
man qmap_open
man qmap
```

If you prefer reading code, the CLI entry point is `src/qmap.c`.

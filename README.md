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

// Option 3: Drop all entries from map (clear entire map)
qmap_drop(by_time);
```

**API Functions**:
- `qmap_get_multi(hd, key)` - Returns a cursor to iterate over all values for a key, or `QM_MISS` if key doesn't exist
- `qmap_count(hd, key)` - Returns the number of entries for a specific key (or total entries if key is NULL)
- `qmap_del(hd, key)` - Deletes only the first occurrence for QM_MULTIVALUE maps
- `qmap_del_all(hd, key)` - Deletes all occurrences for a given key

## Type System

libqmap supports both built-in and custom types for keys and values.

### Built-in Type Constants

| Constant | Description |
|----------|-------------|
| `QM_U32` | 32-bit unsigned integer (4 bytes, ordered) |
| `QM_STR` | Null-terminated string (variable length, ordered lexicographically) |
| `QM_HNDL` | Map handle (uint32_t, used for maps-of-maps) |
| `QM_PTR` | Raw pointer (fixed-size, ordered by address) |

### Custom Type Registration

Register fixed-length or variable-length types for use as keys or values:

```c
uint32_t qm_point = qmap_reg(sizeof(struct point));      // fixed-length
uint32_t qm_blob = qmap_mreg(measure_blob);              // variable-length
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `qmap_reg` | `uint32_t qmap_reg(size_t len)` | Register a fixed-length type. Returns type ID or `QM_MISS`. |
| `qmap_mreg` | `uint32_t qmap_mreg(qmap_measure_t *measure)` | Register a variable-length type. `measure` callback returns byte size. |
| `qmap_type_len` | `size_t qmap_type_len(uint32_t type_id)` | Get fixed byte-length of a type, or 0 if variable-length. |
| `qmap_len` | `size_t qmap_len(uint32_t type_id, const void *data)` | Get byte length of a specific element. |
| `qmap_cmp_set` | `void qmap_cmp_set(uint32_t ref, qmap_cmp_t *cmp)` | Override comparison function for a type. |

**Callback typedefs:**

| Type | Signature | Description |
|------|-----------|-------------|
| `qmap_measure_t` | `size_t (*)(const void *data)` | Returns byte size of a variable-length element. |
| `qmap_cmp_t` | `int (*)(const void *a, const void *b, size_t len)` | Comparison: returns <0, 0, >0. |

## Flags

| Flag | Value | Applies to | Description |
|------|-------|------------|-------------|
| `QM_SORTED` | — | `qmap_open` | Keep entries sorted by key (required for `QM_MULTIVALUE`). |
| `QM_MULTIVALUE` | — | `qmap_open` | Allow multiple values per key (requires `QM_SORTED`). |
| `QM_MIRROR` | — | `qmap_open` | Create bidirectional reverse-lookup mirror (handle + 1). |
| `QM_AINDEX` | — | `qmap_open` | Auto-index: assign sequential integer IDs for each unique key. |
| `QM_NOGROW` | — | `qmap_open` | Disallow auto-growth beyond initial `mask` capacity. |
| `QM_RANGE` | — | `qmap_iter` | Enable ordered range scan over sorted keys. |
| `QM_RECORD()` | — | `qmap_open` | Declare vtype as a record type for field-level access. |

**Sentinel:** `QM_MISS` (`UINT32_MAX`) is returned by `qmap_open`, `qmap_reg`, and `qmap_iter` on failure.

## Iteration

Iterate over all entries in a map, or over entries matching a key:

```c
// Iterate all entries
uint32_t cur = qmap_iter(hd, NULL, 0);
const void *k, *v;
while (qmap_next(&k, &v, cur)) {
    printf("key=%u val=%s\n", *(uint32_t *)k, (const char *)v);
}
qmap_fin(cur);

// Range scan: iterate entries with keys in [2, 5)
uint32_t key_start = 2;
uint32_t cur2 = qmap_iter(hd, &key_start, QM_RANGE);
while (qmap_next(&k, &v, cur2)) {
    uint32_t k32 = *(uint32_t *)k;
    if (k32 >= 5) break;  // past range end
    printf("key=%u val=%s\n", k32, (const char *)v);
}
qmap_fin(cur2);
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `qmap_iter` | `uint32_t qmap_iter(uint32_t hd, const void *key, uint32_t flags)` | Start iteration. `key=NULL` iterates all entries; `QM_RANGE` enables range scan. Returns cursor handle or `QM_MISS`. |
| `qmap_next` | `int qmap_next(const void **key, const void **value, uint32_t cur_id)` | Fetch next key/value from cursor. Returns 1 if valid, 0 if done. |
| `qmap_fin` | `void qmap_fin(uint32_t cur_id)` | End iteration early and free cursor. |

## Associations (Secondary Indexes)

Link a secondary (index) map to a primary map. Puts and deletes on the primary
auto-update the secondary via a callback that extracts the secondary key:

```c
static void index_by_time(const void **skey, const void *pkey,
                           const void *value, void *ud)
{
    (void)pkey; (void)ud;
    const struct interval *iv = value;
    *(const time_t **)skey = &iv->max;  // secondary key = interval.max
}

qmap_assoc(by_time, primary, index_by_time, NULL);
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `qmap_assoc` | `void qmap_assoc(uint32_t hd, uint32_t link, qmap_assoc_t cb, void *userdata)` | Link secondary map to primary. Callback extracts a single secondary key. |
| `qmap_assoc_multi` | `void qmap_assoc_multi(uint32_t hd, uint32_t link, qmap_assoc_multi_t cb, void *userdata)` | Like `qmap_assoc` but callback produces multiple secondary keys per primary entry. |

**Callback typedefs:**

| Type | Signature | Description |
|------|-----------|-------------|
| `qmap_assoc_t` | `void (*)(const void **skey, const void *pkey, const void *value, void *userdata)` | Sets `*skey` to the secondary key. |
| `qmap_assoc_multi_t` | `size_t (*)(const void **skeys, size_t max_skeys, const void *pkey, const void *value, void *userdata)` | Fills `skeys[]` array. Returns count written. |

## Record-Aware Maps

For structured data with named fields, register a record layout and open maps
with `QM_RECORD()` for field-level get/put and automatic reference resolution.

```c
// Register an "author" record with id + name fields
qmap_record_field_t author_fields[] = {
    { "id",   QM_U32,        0, 0 },
    { "name", QM_STR,        0, 0 },
};
uint32_t rec_author = qmap_record_register("author",
    sizeof(struct author), author_fields, 2);

// Open a record-aware map
uint32_t authors = qmap_open("authors.db", "main",
    QM_STR, qmap_record_type_id(rec_author), 0xFF, QM_RECORD());

// Field-level access
qmap_field_put(authors, "alice", "id", "1");
qmap_field_put(authors, "alice", "name", "Alice");
const char *name = qmap_field_get(authors, "alice", "name");
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `qmap_record_register` | `uint32_t qmap_record_register(const char *name, size_t struct_size, const qmap_record_field_t *fields, size_t field_count)` | Register a record layout. Returns record ID or `QM_MISS`. |
| `qmap_record_type_id` | `uint32_t qmap_record_type_id(uint32_t record_id)` | Get type ID for use as `vtype` in `qmap_open`. |
| `qmap_get_key` | `const char *qmap_get_key(uint32_t hd, uint32_t pos)` | Get the string key (item ID) at position `pos`. |
| `qmap_pos` | `uint32_t qmap_pos(uint32_t hd, const char *key)` | Get position number for a key (O(n) linear scan). |
| `qmap_field_put` | `uint32_t qmap_field_put(uint32_t hd, const char *item_id, const char *field_name, const char *value)` | Set a field value. Reference fields auto-resolve string IDs to positions. |
| `qmap_field_get` | `const char *qmap_field_get(uint32_t hd, const char *item_id, const char *field_name)` | Get a field value. Reference fields resolve positions back to string IDs. |
| `qmap_inv_get` | `size_t qmap_inv_get(uint32_t hd, const char *field_name, uint32_t target_pos, uint32_t *out, size_t max)` | Inverse index query: find source positions referencing `target_pos`. |
| `qmap_record_field_set_target_hd` | `void qmap_record_field_set_target_hd(uint32_t record_id, const char *field_name, uint32_t target_hd)` | Configure a reference field's target map handle after both maps exist. |

## ID Management

libqmap provides a free-list ID allocator (`idm_t`) for managing reusable
integer IDs alongside `QM_AINDEX` maps:

```c
idm_t mgr = idm_init();
uint32_t id = idm_new(&mgr);          // allocate (reuses freed IDs first)
idm_del(&mgr, id);                    // free a specific ID
idm_push(&mgr, 100);                  // bulk-push IDs 0..99
idm_drop(&mgr);                       // free all managed IDs
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `idm_init` | `idm_t idm_init(void)` | Initialize empty ID manager. |
| `idm_new` | `uint32_t idm_new(idm_t *idm)` | Allocate a new ID (reuses freed IDs first). |
| `idm_del` | `int idm_del(idm_t *idm, uint32_t id)` | Free a specific ID. Returns 1 if `last` updated. |
| `idm_push` | `uint32_t idm_push(idm_t *idm, uint32_t n)` | Bulk-push IDs 0..n-1 onto free list. |
| `idm_drop` | `void idm_drop(idm_t *idm)` | Free all managed IDs. |
| `ids_init` | `ids_t ids_init(void)` | Initialize raw ID list. |
| `ids_push` | `void ids_push(ids_t *list, uint32_t id)` | Push ID onto free list. |
| `ids_pop` | `uint32_t ids_pop(ids_t *list)` | Pop ID from free list. Returns `IDM_MISS` if empty. |
| `ids_peek` | `uint32_t ids_peek(ids_t *list)` | Peek at top ID without removing. |
| `ids_drop` | `void ids_drop(ids_t *list)` | Free all IDs in list. |
| `ids_iter` | `idsi_t *ids_iter(ids_t *list)` | Get pointer to first node for iteration. |
| `ids_next` | `int ids_next(uint32_t *id, idsi_t **cur)` | Advance iterator. Returns 1 if valid, 0 if done. |

## Full API Reference

| Category | Function | Signature | Description |
|----------|----------|-----------|-------------|
| **Lifecycle** | `qmap_open` | `uint32_t qmap_open(const char *filename, const char *database, uint32_t ktype, uint32_t vtype, uint32_t mask, uint32_t flags)` | Open/create a map. |
| | `qmap_save` | `void qmap_save(void)` | Write all file-backed maps to disk. |
| | `qmap_close` | `void qmap_close(uint32_t hd)` | Close a map and free entries. |
| | `qmap_drop` | `void qmap_drop(uint32_t hd)` | Remove all entries (keep map open). |
| | `qmap_get_vtype` | `uint32_t qmap_get_vtype(uint32_t hd)` | Get value type ID for a map. |
| **CRUD** | `qmap_get` | `const void *qmap_get(uint32_t hd, const void *key)` | Get value by key. |
| | `qmap_put` | `uint32_t qmap_put(uint32_t hd, const void *key, const void *value)` | Insert/update key-value. |
| | `qmap_del` | `void qmap_del(uint32_t hd, const void *key)` | Delete entry by key (first match for MULTIVALUE). |
| | `qmap_del_all` | `void qmap_del_all(uint32_t hd, const void *key)` | Delete all entries matching key. |
| **Iteration** | `qmap_iter` | `uint32_t qmap_iter(uint32_t hd, const void *key, uint32_t flags)` | Start iteration over entries. |
| | `qmap_next` | `int qmap_next(const void **key, const void **value, uint32_t cur_id)` | Next key/value from cursor. |
| | `qmap_fin` | `void qmap_fin(uint32_t cur_id)` | End iteration. |
| | `qmap_get_multi` | `uint32_t qmap_get_multi(uint32_t hd, const void *key)` | Iterate all values for a MULTIVALUE key. |
| | `qmap_count` | `uint32_t qmap_count(uint32_t hd, const void *key)` | Count entries matching key. |
| **Types** | `qmap_reg` | `uint32_t qmap_reg(size_t len)` | Register fixed-length type. |
| | `qmap_mreg` | `uint32_t qmap_mreg(qmap_measure_t *measure)` | Register variable-length type. |
| | `qmap_type_len` | `size_t qmap_type_len(uint32_t type_id)` | Get type byte length. |
| | `qmap_len` | `size_t qmap_len(uint32_t type_id, const void *data)` | Get element byte length. |
| | `qmap_cmp_set` | `void qmap_cmp_set(uint32_t ref, qmap_cmp_t *cmp)` | Override comparison for a type. |
| **Assoc** | `qmap_assoc` | `void qmap_assoc(uint32_t hd, uint32_t link, qmap_assoc_t cb, void *ud)` | Link secondary index. |
| | `qmap_assoc_multi` | `void qmap_assoc_multi(uint32_t hd, uint32_t link, qmap_assoc_multi_t cb, void *ud)` | Link multi-key secondary index. |
| **Records** | `qmap_record_register` | `uint32_t qmap_record_register(const char *name, size_t struct_size, const qmap_record_field_t *fields, size_t field_count)` | Register record layout. |
| | `qmap_record_type_id` | `uint32_t qmap_record_type_id(uint32_t record_id)` | Get type ID for record. |
| | `qmap_get_key` | `const char *qmap_get_key(uint32_t hd, uint32_t pos)` | Get key at position. |
| | `qmap_pos` | `uint32_t qmap_pos(uint32_t hd, const char *key)` | Get position for key. |
| | `qmap_field_put` | `uint32_t qmap_field_put(uint32_t hd, const char *item_id, const char *field_name, const char *value)` | Set field value. |
| | `qmap_field_get` | `const char *qmap_field_get(uint32_t hd, const char *item_id, const char *field_name)` | Get field value. |
| | `qmap_inv_get` | `size_t qmap_inv_get(uint32_t hd, const char *field_name, uint32_t target_pos, uint32_t *out, size_t max)` | Inverse index query. |
| | `qmap_record_field_set_target_hd` | `void qmap_record_field_set_target_hd(uint32_t record_id, const char *field_name, uint32_t target_hd)` | Set reference field target. |
| **ID mgmt** | `idm_init` | `idm_t idm_init(void)` | Init ID manager. |
| | `idm_new` | `uint32_t idm_new(idm_t *idm)` | Allocate ID. |
| | `idm_del` | `int idm_del(idm_t *idm, uint32_t id)` | Free ID. |
| | `idm_push` | `uint32_t idm_push(idm_t *idm, uint32_t n)` | Bulk-push IDs. |
| | `idm_drop` | `void idm_drop(idm_t *idm)` | Free all IDs. |
| | `ids_init` | `ids_t ids_init(void)` | Init raw ID list. |
| | `ids_push` | `void ids_push(ids_t *list, uint32_t id)` | Push ID. |
| | `ids_pop` | `uint32_t ids_pop(ids_t *list)` | Pop ID. |
| | `ids_peek` | `uint32_t ids_peek(ids_t *list)` | Peek at top ID. |
| | `ids_drop` | `void ids_drop(ids_t *list)` | Free all IDs in list. |
| | `ids_iter` | `idsi_t *ids_iter(ids_t *list)` | Start iteration. |
| | `ids_next` | `int ids_next(uint32_t *id, idsi_t **cur)` | Next ID. |

## Docs
Use the man pages for complete library and CLI documentation:
```sh
man qmap_open
man qmap
```

If you prefer reading code, the CLI entry point is `src/qmap.c`.

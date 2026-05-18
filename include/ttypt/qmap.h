#ifndef QMAP_H
#define QMAP_H

/**
 * @file qmap.h
 * @brief Public header for the Qmap library.
 *
 * Declares the API for Qmap — associative containers
 * with optional persistence, mirror maps and
 * sorted iteration.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

/**
 * @brief Sentinel value for "not found" or error conditions.
 *
 * Returned by qmap_reg(), qmap_mreg() when type limit is reached,
 * and used internally to indicate missing entries.
 * Equal to UINT32_MAX.
 */
#define QM_MISS ((uint32_t)-1)

/** @defgroup qmap_constants Qmap constants
 *  @brief Constant definitions for flags and built-in types.
 *  @see qmap_common
 *  @see qmap_assoc
 *  @see qmap_iteration
 *  @see qmap_type
 *  @{
 */

/**
 * @brief QMap flags.
 */
enum qmap_flags {
  /** Auto–index for NULL keys. When enabled, passing NULL
   *  as the key to qmap_put generates auto-incrementing IDs. */
  QM_AINDEX = 1,

  /** Create reverse-lookup (secondary) map. The mirror map
   *  handle is always primary_hd + 1 and swaps keys/values.
   *  
   *  QM_MIRROR is useful when you need bidirectional lookup (key→value
   *  and value→key) and is commonly used with file persistence.
   *  
   *  The mirror map is automatically closed when you close the primary
   *  map via qmap_close(hd). */
  QM_MIRROR = 2,

  /** For associated maps: default to obtaining primary keys
   *  instead of values. Used internally for mirror maps and
   *  essential for creating value→key secondary indexes.
   *  
   *  When a secondary map has QM_PGET, qmap_get() returns
   *  the primary key instead of the primary value. This allows
   *  secondary indexes to map from values back to keys. */
  QM_PGET = 4,

  /** Enable sorted index support (B-tree search). Enables
   *  ordered iteration. Index is automatically rebuilt on
   *  modifications (put/del operations mark it dirty).
   *  
   *  Performance Note: The sorted index is rebuilt from scratch
   *  whenever it's marked dirty and iteration is requested.
   *  This makes the first iteration after modifications O(n log n)
   *  instead of O(n). */
  QM_SORTED = 8,

  /** Allow duplicate keys in sorted maps. Enables multi-value
   *  lookups where multiple entries can share the same key.
   *  
   *  REQUIRES QM_SORTED: This flag cannot be used without QM_SORTED.
   *  qmap_open() will return QM_MISS if QM_MULTIVALUE is set without
   *  QM_SORTED.
   *  
   *  Behavior:
   *  - qmap_put() with an existing key ADDS a new entry (does not replace)
   *  - qmap_get() returns the FIRST matching value
   *  - qmap_get_multi() returns cursor to iterate over ALL matching values
   *  - qmap_count() returns the number of entries for a key
   *  - qmap_del() deletes only the FIRST matching entry
   *  - qmap_del_all() deletes ALL entries with the specified key
   *  
   *  Use Case: Secondary indexes via qmap_assoc() where multiple primary
   *  entries map to the same secondary key. */
  QM_MULTIVALUE = 16,

  /** Disable auto-grow. When set and the map reaches capacity,
   *  inserts return QM_MISS instead of growing the table.
   *  Use for memory-constrained environments or fixed-size tables. */
  QM_NOGROW = 32,
};

/**
 * @brief Macro and mask for record-aware maps.
 *
 * Pass QM_RECORD(record_id) as the flags parameter to qmap_open to
 * enable record-aware access.  The map must have ktype=QM_STR and
 * vtype equal to the struct type ID returned by
 * qmap_record_type_id(record_id).
 *
 * When a map is record-aware, keys containing ':' are treated as
 * composite keys "struct_key:field_name" and resolve to a pointer
 * directly into the stored struct's pool allocation at the field's
 * byte offset.  This allows whole-struct storage and per-field access
 * to coexist without data duplication.
 */
#define QM_RECORD_MASK   0x0000FF00u
#define QM_RECORD_FLAG   0x00010000u
#define QM_RECORD_ID(f)  (((f) & QM_RECORD_MASK) >> 8)
#define QM_RECORD(id)    (QM_RECORD_FLAG | (((id) & 0xFF) << 8))

/**
 * @brief Built-in type identifiers.
 */
enum qmap_tbi {
  /** Pointer (hashed). */
  QM_PTR  = 0,

  /** Opaque handle. Uses value directly as hash (no transformation). */
  QM_HNDL = 1,

  /** String contents hash and compare. */
  QM_STR  = 2,

  /** 32-bit unsigned integer (hash and mask). */
  QM_U32  = 3,
};

/**
 * @brief Record field type constants.
 *
 * These are only meaningful as qmap_record_field_t.type values,
 * NOT as key/value types for qmap_open().
 */
#define QM_REFERENCE          6  /**< Single uint32_t position in target's map. */
#define QM_MULTI_REFERENCE    7  /**< \n-separated positions in char[max_size]. */
#define QM_VSTR               8  /**< Variable-length string stored outside the struct. */

/**
 * @brief Iterator flags.
 */
enum qmap_if {
  /** Continue iteration even if key differs from the initial.
   *  Behavior depends on whether QM_SORTED was set on the map:
   *  - With QM_SORTED: performs ordered range scan from the
   *    starting key onwards (B-tree ordered iteration)
   *  - Without QM_SORTED: performs linear scan through the
   *    hash table, comparing keys with the initial key using
   *    the type's comparison function */
  QM_RANGE = 1,
};

/** @} */

/** @defgroup qmap_handle Qmap open, close and save
 *  @brief Functions for opening, closing and saving maps.
 *
 *  @note Qmap uses global state and is not thread-safe.
 *
 *  @note Capacity Limits: Initial capacity is mask + 1. When capacity is
 *        reached, the map automatically grows (doubles) unless QM_NOGROW
 *        is set. Growth is transparent to callers.
 *
 *  @note Memory Allocation: Malloc failures trigger CBUG() which terminates
 *        the process immediately. There is no graceful error handling for
 *        out-of-memory conditions.
 *
 *  @warning File-backed maps are automatically saved at process exit.
 *           Explicit qmap_save() calls are only needed for mid-execution
 *           persistence. The automatic save is triggered by a library
 *           destructor function registered during initialization.
 *  @{
 */

/**
 * @brief Open a database.
 *
 * Creates an in-memory map and registers its handle
 * with the internal file cache, linking it to
 * 'filename'. If a file exists, it loads the map
 * data for the specified 'database'.
 *
 * @param[in] filename Path to file or cache key.
 *                     NULL → in-memory only.
 * @param[in] database Logical name within file.
 *                     NULL → skip file association.
 * @param[in] ktype    Built-in or registered key
 *                     type.
 * @param[in] vtype    Built-in or registered value
 *                     type.
 * @param[in] mask     Must be 2ⁿ − 1; table size is
 *                     (mask + 1).
 * @param[in] flags    Bitwise OR of QM_AINDEX,
 *                     QM_MIRROR, QM_SORTED, etc.
 * @return             Map handle (hd).
 *
 * @note File Persistence: File-backed maps automatically load
 *       data from disk when opened, regardless of flags. The
 *       QM_MIRROR flag enables bidirectional lookup (creating a
 *       reverse map at handle hd + 1) which is useful for many
 *       persistence scenarios. The mirror map is automatically
 *       closed when closing the primary map.
 *
 * @note Multiple databases can share a single file. Each database
 *       is identified by a hash of its name (XXH32). Data is saved
 *       and loaded based on this database ID.
 */
uint32_t qmap_open(const char *filename,
                   const char *database,
                   uint32_t ktype,
                   uint32_t vtype,
                   uint32_t mask,
                   uint32_t flags);

/**
 * @brief Returns the value type (vtype) of a map.
 */
uint32_t qmap_get_vtype(uint32_t hd);

/**
 * @brief Returns the fixed length of a type, or 0 if variable.
 */
size_t qmap_type_len(uint32_t type_id);

/**
 * @brief Write all file-backed maps to disk.
 *
 * Walks the internal cache, computes file sizes,
 * and performs mmap/memcpy writes for maps
 * associated with a file.
 *
 * @note This is automatically called at process exit.
 *       Explicit calls are only needed for mid-execution
 *       checkpointing or when you want to ensure data
 *       is persisted before continuing.
 */
void qmap_save(void);

/**
 * @brief Close a map and free its resources.
 *
 * Deletes all entries, closes associated secondary maps,
 * and frees internal structures. All open maps are
 * automatically closed at process exit by the library
 * destructor, but you can call this explicitly to free
 * resources earlier.
 *
 * @param[in] hd Handle to close.
 */
void qmap_close(uint32_t hd);

/** @} */

/** @defgroup qmap_common Qmap get, put, del and drop
 *  @brief Core key/value operations.
 *
 *  @warning Pointer Ownership and Lifetime:
 *           All pointers returned by qmap functions (qmap_get, qmap_next)
 *           are owned by the map. They remain valid until:
 *           - The entry is DELETED (qmap_del)
 *           - The map is CLOSED (qmap_close)
 *           - The entry is REPLACED with a LARGER value (qmap_put)
 *           - The key is CHANGED (qmap_put with different key at same hash)
 *
 *           IMPROVED BEHAVIOR: Since v0.6.0, pointers typically remain valid
 *           when updating with the same key and a same-or-smaller value.
 *           However, for maximum safety, it's still recommended to copy
 *           data before modifications.
 *
 *           Do NOT:
 *           - Free returned pointers
 *           - Use pointers after the entry is deleted
 *           - Store pointers long-term without copying the data
 *
 *           SAFE patterns after allocation reuse improvement:
 *           @code
 *           // Updating same key with same-sized value - pointer usually stays valid
 *           uint32_t *counter = qmap_get(hd, key);
 *           qmap_put(hd, key, &(uint32_t){*counter + 1});
 *           // counter pointer is still valid (same allocation reused)
 *           
 *           // For maximum safety, still copy before complex operations:
 *           const uint32_t *old_ptr = qmap_get(hd, key);
 *           uint32_t old_val = old_ptr ? *old_ptr : 0;
 *           qmap_put(hd, key, &new_val);
 *           use(old_val);  // Safe - using copied value
 *           @endcode
 *
 *           UNSAFE pattern (pointer invalidated by larger value):
 *           @code
 *           const char *str = qmap_get(hd, key);  // str = "short"
 *           qmap_put(hd, key, "much longer string");
 *           // str is now INVALID - new allocation was needed
 *           @endcode
 *
 *  @note For QM_PTR type values, the returned pointer points to the
 *        stored pointer bytes, not the pointer itself.
 *
 *  @see qmap_handle
 *  @see qmap_assoc
 *  @see qmap_iteration
 *  @see qmap_type
 *  @{
 */

/**
 * @brief Retrieve a value by key.
 *
 * For maps with QM_MULTIVALUE flag, this returns the FIRST
 * matching value only. To retrieve all values for a key, use
 * qmap_get_multi() instead.
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key to look up.
 * @return        Pointer to value or NULL if not found.
 *                For QM_MULTIVALUE maps, returns first match.
 *                See qmap_common for pointer ownership rules.
 */
const void *qmap_get(uint32_t hd,
                     const void * const key);

/**
 * @brief Insert or update a pair.
 *
 * Behavior depends on the QM_MULTIVALUE flag:
 * - Without QM_MULTIVALUE: Replaces existing value if key exists
 * - With QM_MULTIVALUE: Always adds a new entry (duplicates allowed)
 *
 * @param[in] hd    Map handle.
 * @param[in] key   Key (NULL if QM_AINDEX).
 * @param[in] value Value to store.
 * @return          Internal index for the entry. With QM_AINDEX,
 *                  this is the generated key ID.
 */
uint32_t qmap_put(uint32_t hd,
                  const void * const key,
                  const void * const value);

/**
 * @brief Delete an entry by key.
 *
 * For maps with QM_MULTIVALUE flag, this only deletes the FIRST
 * occurrence of the key. To delete all duplicates, call this
 * function multiple times until the key no longer exists.
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key to delete.
 */
void qmap_del(uint32_t hd,
              const void * const key);

/**
 * @brief Delete all entries with the specified key.
 *
 * For QM_MULTIVALUE maps, removes all duplicate entries. For regular maps,
 * behaves identically to qmap_del().
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key of entries to delete.
 *
 * @see qmap_del
 * @see qmap_get_multi
 */
void qmap_del_all(uint32_t hd, const void * const key);

/**
 * @brief Remove all entries from a map.
 *
 * @param[in] hd Map handle.
 */
void qmap_drop(uint32_t hd);

/** @} */

/** @defgroup qmap_assoc Qmap associations
 *  @brief Linking of primary and secondary maps.
 *  @see qmap_handle
 *  @see qmap_common
 *  @see qmap_iteration
 *  @see qmap_type
 *  @{
 */

/**
 * @brief Association callback type.
 *
 * After association, future puts/dels on the
 * primary will update the secondary.
 * Deletes on the primary remove corresponding
 * entries from the secondary.
 *
 * @param[out] skey     Pointer to set secondary key.
 * @param[in]  pkey     Primary key.
 * @param[in]  value    Primary value.
 * @param[in]  userdata User context pointer (from qmap_assoc call).
 */
typedef void qmap_assoc_t(
  const void **skey,
  const void * const pkey,
  const void * const value,
  void *userdata);

/**
 * @brief Make an association between tables.
 *
 * Links a secondary (index) map to a primary map so that
 * put/delete operations on the primary automatically update
 * the secondary. The callback determines the secondary key.
 *
 * @param[in] hd       Secondary (index) map handle.
 * @param[in] link     Primary (source) map handle.
 * @param[in] cb       Callback to produce secondary
 *                     keys. NULL → use primary value.
 * @param[in] userdata User context pointer passed to callback.
 *
 * @note The secondary map stores (secondary_key, primary_value).
 *       To retrieve the primary KEY instead of the primary VALUE,
 *       the secondary map must be created with the QM_PGET flag.
 *       This is essential for creating value→key indexes.
 *
 * @code
 * // Example: Create a secondary index from username -> user_id
 * // Primary: user_id -> username
 * uint32_t users = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
 * 
 * // Secondary: username -> user_id
 * // QM_PGET makes qmap_get return the primary key instead of value
 * uint32_t by_name = qmap_open(NULL, NULL, QM_STR, QM_U32, 0xFF, QM_PGET);
 * 
 * // Callback: use primary value (username) as secondary key
 * static void value_to_key(const void **skey,
 *                          const void *pkey,
 *                          const void *value) {
 *     (void) pkey;
 *     *skey = value;  // Use username as key
 * }
 * 
 * qmap_assoc(by_name, users, value_to_key);
 * 
 * // Now puts to 'users' automatically update 'by_name'
 * qmap_put(users, &(uint32_t){100}, "alice");
 * // by_name now contains: "alice" -> 100
 * @endcode
 */
void qmap_assoc(uint32_t hd,
                uint32_t link,
                qmap_assoc_t cb,
                void *userdata);

/**
 * @brief Multi-key association callback type.
 *
 * Produces multiple secondary keys from a single primary entry.
 * Used by qmap_assoc_multi for fields that reference multiple values
 * (e.g., multi-reference dataset fields).
 *
 * Callback fills skeys[0..returned_count-1] with key pointers.
 * Each key is copied by qmap internally; callback-owned temporary
 * copies must remain valid until the callback returns.
 *
 * @param[out] skeys     Array to fill with secondary key pointers.
 * @param[in]  max_skeys Capacity of skeys array.
 * @param[in]  pkey      Primary key.
 * @param[in]  value     Primary value.
 * @param[in]  userdata  User context pointer (from qmap_assoc_multi call).
 * @return               Number of keys written to skeys.
 */
typedef size_t qmap_assoc_multi_t(
	const void **skeys,
	size_t max_skeys,
	const void *pkey,
	const void *value,
	void *userdata);

/**
 * @brief Make a multi-key association between tables.
 *
 * Like qmap_assoc, but the callback produces multiple secondary keys
 * from a single primary entry. The secondary is a root map (independent
 * position space) with KEY=vtype=QM_STR, VALUE=ktype=QM_STR, and
 * QM_MULTIVALUE|QM_SORTED flags. Each entry stores (ref_value, primary_key).
 *
 * Puts to the primary automatically insert entries into the secondary.
 * Deletes from the primary automatically remove matching secondary entries.
 *
 * @param[in] hd       Secondary (index) map handle (root map, QM_STR/QM_STR).
 * @param[in] link     Primary (source) map handle.
 * @param[in] cb       Multi-key callback.
 * @param[in] userdata User context pointer passed to callback.
 */
void qmap_assoc_multi(uint32_t hd,
                      uint32_t link,
                      qmap_assoc_multi_t cb,
                      void *userdata);

/** @} */

/** @defgroup qmap_iteration Qmap iteration
 *  @brief Iteration through map contents.
 *  @see qmap_handle
 *  @see qmap_common
 *  @see qmap_assoc
 *  @see qmap_type
 *
 * @code
 * // Sorted range scan (requires QM_SORTED in qmap_open).
 * uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32,
 *                         0xFF, QM_SORTED);
 * uint32_t start = 100;
 * uint32_t cur = qmap_iter(hd, &start, QM_RANGE);
 * const void *key, *value;
 * while (qmap_next(&key, &value, cur)) {
 *     // handle key/value
 * }
 * qmap_fin(cur);
 * @endcode
 *  @{
 */

/**
 * @brief Start iteration.
 *
 * Creates a cursor for iterating over map entries.
 * Ordered ranges require QM_SORTED flag on the map.
 *
 * @param[in] hd    Map handle.
 * @param[in] key   Starting key or NULL for all entries.
 * @param[in] flags Iterator flags (QM_RANGE valid).
 *                  - QM_RANGE with QM_SORTED: ordered scan
 *                  - QM_RANGE without QM_SORTED: linear scan
 *                  - No flags: iterate single key (or all if key is NULL)
 * @return          Cursor handle for use with qmap_next.
 */
uint32_t qmap_iter(uint32_t hd,
                   const void * const key,
                   uint32_t flags);

/**
 * @brief Fetch next key/value.
 *
 * @param[out] key    Pointer to key.
 * @param[out] value  Pointer to value.
 * @param[in]  cur_id Cursor handle.
 * @return            1 if valid, 0 if done.
 *                    See qmap_common for pointer ownership rules.
 */
int qmap_next(const void **key,
              const void **value,
              uint32_t cur_id);

/**
 * @brief End iteration early.
 *
 * @param[in] cur_id Cursor handle.
 */
void qmap_fin(uint32_t cur_id);

/**
 * @brief Start iteration over all values for a key.
 *
 * For maps with QM_MULTIVALUE flag, this returns a cursor that
 * iterates over ALL values associated with the given key in
 * sorted order. For maps without QM_MULTIVALUE, this behaves
 * like a single-value iterator.
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key to look up.
 * @return        Cursor handle for use with qmap_next(), or
 *                QM_MISS if key not found.
 *
 * Example:
 * @code
 * uint32_t cur = qmap_get_multi(hd, &key);
 * if (cur != QM_MISS) {
 *   const void *k, *v;
 *   while (qmap_next(&k, &v, cur)) {
 *     // Process each value for this key
 *   }
 *   qmap_fin(cur);
 * }
 * @endcode
 *
 * @note Internally, this is equivalent to: qmap_iter(hd, key, 0)
 * @note For single-value lookups, qmap_get() is more efficient
 * @see qmap_del_all for deleting all duplicates at once
 * @see qmap_count for counting entries without iteration
 */
uint32_t qmap_get_multi(uint32_t hd, const void *key);

/**
 * @brief Count entries matching a key.
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key to count. NULL counts total entries in map.
 * @return        Number of matching entries.
 *
 * @note For QM_MULTIVALUE maps, returns count of all duplicate values
 * @note For normal maps, returns 0 or 1
 */
uint32_t qmap_count(uint32_t hd, const void *key);

/** @} */

/** @defgroup qmap_type Qmap type customization
 *  @brief Functions for registering and managing key/value types.
 *
 *  @note Type Limits: There is a compile-time limit on the number of
 *        custom types that can be registered. When the limit is reached,
 *        qmap_reg() and qmap_mreg() return QM_MISS and print an error
 *        message to stderr. The limit is determined by TYPES_MASK in
 *        the implementation.
 *
 *  @see qmap_handle
 *  @see qmap_common
 *  @see qmap_assoc
 *  @see qmap_iteration
 *  @{
 */

/**
 * @brief Callback to measure variable-size keys.
 *
 * Keys of dynamic length need measurement when
 * hashing/comparing beyond pointer equality.
 *
 * @param[in] data Pointer to key.
 * @return         Key size in bytes.
 */
typedef size_t qmap_measure_t(const void *data);

/**
 * @brief Register a fixed-length type.
 *
 * Registers a new custom type with a fixed byte length.
 * The type will use the default hash (XXH32) and
 * comparison (memcmp) functions.
 *
 * @param[in] len Length in bytes.
 * @return        Type ID for use in qmap_open, or
 *                QM_MISS if type limit is reached.
 */
uint32_t qmap_reg(size_t len);

/**
 * @brief Comparison callback type.
 *
 * @param[in] a   First object.
 * @param[in] b   Second object.
 * @param[in] len Length in bytes.
 * @return        <0, 0, or >0.
 */
typedef int qmap_cmp_t(
  const void * const a,
  const void * const b,
  size_t len);

/**
 * @brief Assign comparison function to a type.
 *
 * @param[in] ref Type ID.
 * @param[in] cmp Comparison callback.
 */
void qmap_cmp_set(uint32_t ref,
                  qmap_cmp_t *cmp);

/**
 * @brief Register a variable-length type.
 *
 * Registers a new custom type with variable length.
 * A measurement callback is required to determine the
 * size of each element. The type will use the default
 * hash (XXH32) and comparison (memcmp) functions.
 *
 * @param[in] measure Size-measuring callback.
 * @return            Type ID for use in qmap_open, or
 *                    QM_MISS if type limit is reached.
 */
uint32_t qmap_mreg(qmap_measure_t *measure);

/**
 * @brief Get the byte length of an element.
 *
 * @param[in] type_id Type ID.
 * @param[in] data    Element pointer.
 * @return            Size in bytes.
 */
size_t qmap_len(uint32_t type_id,
                const void *data);

/** @defgroup qmap_record Record-Aware Maps
 *  @brief Functions for record-aware (struct-aware) maps.
 *
 * Record-aware maps store C struct values by-key and automatically
 * resolve composite keys (e.g. "id:fieldname") to field offsets
 * within the stored struct.  No per-field entries are stored — the
 * struct is the single source of truth.
 *
 * Usage:
 * @code
 * typedef struct { char title[256]; uint32_t age; } item_t;
 *
 * uint32_t rec = qmap_record_register("item",
 *     sizeof(item_t),
 *     (qmap_record_field_t[]){
 *         { "title", QM_STR, offsetof(item_t, title),
 *           sizeof(((item_t*)0)->title) },
 *         { "age",   QM_U32, offsetof(item_t, age),
 *           sizeof(uint32_t) },
 *     }, 2);
 *
 * uint32_t hd = qmap_open(NULL, NULL, QM_STR,
 *                         qmap_record_type_id(rec),
 *                         0xFF, QM_RECORD(rec));
 *
 * item_t row = { .title = "Hello", .age = 42 };
 * qmap_put(hd, "item1", &row);        // store whole struct
 * const char *t = qmap_get(hd, "item1:title");  // → &row.title
 * const uint32_t *a = qmap_get(hd, "item1:age"); // → &row.age
 * @endcode
 *
 * @{
 */

/**
 * @brief Describes a single field in a record layout.
 * @see qmap_record_register
 */
typedef struct {
  const char *name;      /**< Field name (e.g. "title"). */
  uint32_t type;         /**< QM_STR, QM_U32, QM_REFERENCE, QM_MULTI_REFERENCE. */
  size_t offset;         /**< offsetof(struct_type, field). */
  size_t max_size;       /**< Buffer capacity for inline QM_STR / QM_MULTI_REFERENCE arrays. */
  uint32_t target_record; /**< Record ID of the target map (0 = none). */
  uint32_t target_hd;     /**< Head handle for target map (qmap_field_put auto-resolve). Set via qmap_record_field_set_target_hd(). */
  const char *inverse;   /**< Field name on target for inverse lookups, or NULL. */
} qmap_record_field_t;

/**
 * @brief Register a record layout describing a C struct.
 *
 * Internally calls qmap_reg(struct_size) to create a fixed-length
 * type for the struct.  The returned record_id is used with
 * QM_RECORD(record_id) in qmap_open().
 *
 * @param[in] name         Record name (for debugging, copied internally).
 * @param[in] struct_size  sizeof(struct).
 * @param[in] fields       Array of field descriptors.
 * @param[in] field_count  Number of fields.
 * @return Record ID for use with QM_RECORD(), or QM_MISS on failure.
 */
uint32_t qmap_record_register(
  const char *name,
  size_t struct_size,
  const qmap_record_field_t *fields,
  size_t field_count);

/**
 * @brief Get the struct type ID registered for a record.
 *
 * @param[in] record_id  Record ID from qmap_record_register().
 * @return Type ID for use as vtype in qmap_open(), or QM_MISS.
 */
uint32_t qmap_record_type_id(uint32_t record_id);

/**
 * @brief Get the string key at a given position number.
 *
 * For record-aware maps this is the item ID (e.g. "choir1").
 *
 * @param[in] hd   Map handle.
 * @param[in] pos  Position number.
 * @return         Key string, or NULL if pos is out of range.
 */
const char *qmap_get_key(uint32_t hd, uint32_t pos);

/**
 * @brief Get the position number for a given key string.
 *
 * Performs an O(n) linear scan of the map entries.  Used when
 * converting filesystem string IDs to position numbers for
 * reference fields.
 *
 * @param[in] hd   Map handle (must be record-aware, QM_STR keys).
 * @param[in] key  Key string to look up.
 * @return         Position number, or UINT32_MAX if not found.
 */
uint32_t qmap_pos(uint32_t hd, const char *key);

/**
 * @brief Query the inverse index for a reference field.
 *
 * Returns all source positions that reference @p target_pos
 * via the named reference field.
 *
 * @param[in]  hd          Map handle.
 * @param[in]  field_name  Reference field name.
 * @param[in]  target_pos  Position in the target record-aware map.
 * @param[out] out         Array to fill with source positions.
 * @param[in]  max         Capacity of out[].
 * @return Number of positions written to out[].
 */
size_t qmap_inv_get(uint32_t hd, const char *field_name,
                    uint32_t target_pos,
                    uint32_t *out, size_t max);

/**
 * @brief Set the target head handle for reference resolution.
 *
 * After both source and target maps exist, configure a reference
 * field so that qmap_field_put() can auto-resolve string IDs
 * to positions via qmap_pos(target_hd, id).
 *
 * @param[in] record_id    Record ID from qmap_record_register().
 * @param[in] field_name   Field name.
 * @param[in] target_hd    Head handle of the target map (e.g. song.items).
 */
void qmap_record_field_set_target_hd(uint32_t record_id,
                                     const char *field_name,
                                     uint32_t target_hd);

/**
 * @brief Put a field value into a record-aware map, auto-resolving
 *        references when the field is QM_REFERENCE / QM_MULTI_REFERENCE
 *        and target_hd is set.
 *
 * For QM_REFERENCE: resolves @p value (a string ID) to a position via
 * qmap_pos(target_hd, value) before calling qmap_put.
 * For QM_MULTI_REFERENCE: splits @p value on newlines, resolves each
 * ID, and joins the positions with newlines.
 * For QM_STR: passes @p value through unchanged.
 *
 * @return qmap_put result (position number, or 0 on error).
 */
uint32_t qmap_field_put(uint32_t hd, const char *item_id,
                        const char *field_name, const char *value);

/**
 * @brief Get a field value from a record-aware map, auto-resolving
 *        references when the field is QM_REFERENCE.
 *
 * Inverse of qmap_field_put().
 *
 * For QM_STR: returns the string value directly (points into struct memory).
 * For QM_REFERENCE: resolves the stored position back to the target ID
 * string via qmap_get_key(target_hd, pos).
 * For QM_MULTI_REFERENCE and other types: returns the raw value pointer
 * (caller must know the type).
 *
 * @param[in] hd         Record-aware map handle.
 * @param[in] item_id    Record key (entry ID).
 * @param[in] field_name Field name.
 * @return               String value (qmap-managed, do not free), or NULL.
 */
const char *qmap_field_get(uint32_t hd, const char *item_id,
                           const char *field_name);

/** @} */

#endif /* QMAP_H */

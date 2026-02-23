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
   *  File-backed maps will load data from disk regardless of this flag.
   *  QM_MIRROR is useful when you need bidirectional lookup (key→value
   *  and value→key) and is commonly used with file persistence.
   *  
   *  When using QM_MIRROR, remember to close both maps:
   *  qmap_close(hd) and qmap_close(hd + 1). */
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
};

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
 *  @note Capacity Limits: Map capacity is determined by the mask parameter
 *        and cannot be changed after creation. Capacity = mask + 1.
 *        Attempting to exceed capacity terminates the process via CBUG().
 *        There is no dynamic resizing.
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
 *       persistence scenarios. When using QM_MIRROR, both the
 *       primary (hd) and mirror (hd + 1) maps must be closed.
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
 *           IMPROVED BEHAVIOR: Since version with allocation reuse, pointers
 *           typically remain valid when updating with the same key and a
 *           same-or-smaller value. However, for maximum safety, it's still
 *           recommended to copy data before modifications.
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
 * @param[in] hd  Map handle.
 * @param[in] key Key to look up.
 * @return        Pointer to value or NULL if not found.
 *                See qmap_common for pointer ownership rules.
 */
const void *qmap_get(uint32_t hd,
                     const void * const key);

/**
 * @brief Insert or update a pair.
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
 * @param[in] hd  Map handle.
 * @param[in] key Key to delete.
 */
void qmap_del(uint32_t hd,
              const void * const key);

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
 * @param[out] skey  Pointer to set secondary key.
 * @param[in]  pkey  Primary key.
 * @param[in]  value Primary value.
 */
typedef void qmap_assoc_t(
  const void **skey,
  const void * const pkey,
  const void * const value);

/**
 * @brief Make an association between tables.
 *
 * Links a secondary (index) map to a primary map so that
 * put/delete operations on the primary automatically update
 * the secondary. The callback determines the secondary key.
 *
 * @param[in] hd   Secondary (index) map handle.
 * @param[in] link Primary (source) map handle.
 * @param[in] cb   Callback to produce secondary
 *                 keys. NULL → use primary value.
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
                qmap_assoc_t cb);

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

/** @} */

#endif /* QMAP_H */

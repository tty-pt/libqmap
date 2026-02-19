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
  /** Auto–index for NULL keys. */
  QM_AINDEX = 1,

  /** Create reverse-lookup (secondary) map. */
  QM_MIRROR = 2,

  /** Default to obtaining primary keys instead of
   *  values. */
  QM_PGET = 4,

  /** Enable sorted index support (BTREE search). */
  QM_SORTED = 8,
};

/**
 * @brief Built-in type identifiers.
 */
enum qmap_tbi {
  /** Pointer (hashed). */
  QM_PTR  = 0,

  /** Opaque handle (no hashing). */
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
  /** Continue iteration even if key differs from
   *  the initial. */
  QM_RANGE = 1,
};

/** @} */

/** @defgroup qmap_handle Qmap open, close and save
 *  @brief Functions for opening, closing and saving maps.
 *
 *  @note Qmap uses global state and is not thread-safe.
 *        File-backed maps are saved automatically at process exit.
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
 * associated with a file. (automatic at exit)
 */
void qmap_save(void);

/**
 * @brief Close a map (usually automatic).
 *
 * @param[in] hd Handle to close.
 */
void qmap_close(uint32_t hd);

/** @} */

/** @defgroup qmap_common Qmap get, put, del and drop
 *  @brief Core key/value operations.
 *  @see qmap_handle
 *  @see qmap_assoc
 *  @see qmap_iteration
 *  @see qmap_type
 *  @{
 */

/**
 * @brief Retrieve a value by key.
 *
 * Returned pointers are owned by the map. They remain valid
 * until the entry is replaced, deleted, or the map is closed.
 * Do not free the returned pointer. For QM_PTR values, the
 * returned pointer points to the stored pointer bytes.
 *
 * @param[in] hd  Map handle.
 * @param[in] key Key to look up.
 * @return        Pointer to value or NULL.
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
 * @param[in] hd   Secondary (index) map handle.
 * @param[in] link Primary (source) map handle.
 * @param[in] cb   Callback to produce secondary
 *                 keys. NULL → use primary value.
 *
 * @code
 * // Build a secondary index from value -> key.
 * static void value_to_key(const void **skey,
 *                           const void *pkey,
 *                           const void *value) {
 *     (void) pkey;
 *     *skey = value;
 * }
 *
 * uint32_t primary = qmap_open("data.qmap", "primary",
 *                              QM_U32, QM_STR,
 *                              0xFF, QM_MIRROR);
 * qmap_assoc(primary + 1, primary, value_to_key);
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
 * Ordered ranges require QM_SORTED.
 *
 * @param[in] hd    Map handle.
 * @param[in] key   Starting key or NULL.
 * @param[in] flags QM_RANGE valid.
 * @return          Cursor handle.
 */
uint32_t qmap_iter(uint32_t hd,
                   const void * const key,
                   uint32_t flags);

/**
 * @brief Fetch next key/value.
 *
 * Returned pointers are owned by the map. They remain valid
 * until the entry is replaced, deleted, or the map is closed.
 * Do not free the returned pointers.
 *
 * @param[out] key    Pointer to key.
 * @param[out] value  Pointer to value.
 * @param[in]  cur_id Cursor handle.
 * @return            1 if valid, 0 if done.
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
 * @param[in] len Length in bytes.
 * @return        Type ID.
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
 * @param[in] measure Size-measuring callback.
 * @return            Type ID.
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

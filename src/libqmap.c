/* libqmap.c
 * Licence: BSD-2-Clause
 *
 * I'm adding some comments to make it easier to understand,
 * but whatever's user API is documented in the header file.
 */
#include "./../include/ttypt/qmap.h"
#include "./../include/ttypt/idm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xxhash.h>
#include <ttypt/qsys.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* MACROS, STRUCTS, ENUMS AND GLOBALS {{{ */

#define QM_SEED 13
#define QM_DEFAULT_MASK 0xFF
#define QM_MAX 1024
#define QMAP_POOL_STEP 16
#define QMAP_POOL_MAX 4096
#define QMAP_POOL_BINS (QMAP_POOL_MAX / QMAP_POOL_STEP)

#define TYPES_MASK 0xFF

#define DEBUG_LVL 1

#define DEBUG(lvl, ...) \
  if (DEBUG_LVL > lvl) WARN(__VA_ARGS__)

#define VAL_ADDR(qmap, n) \
  (void **)(((char *) qmap->table) \
      + sizeof(void *) * n)

typedef struct qmap_blk {
  struct qmap_blk *next;
  size_t size;
} qmap_blk_t;

  static inline size_t
qmap_payload_off(size_t key_len)
{
  size_t align = sizeof(void *) - 1;

  return (key_len + align) & ~align;
}

static_assert(QM_MISS == UINT32_MAX, "assume U32INT_MAX");

enum QM_MBR {
  QM_KEY,
  QM_VALUE,
};

enum qm_internal_flags {
  QM_SDIRTY = 1, // sorted list needs rebuild
  QM_IS_MIRROR = 2,  // this is a QM_MIRROR map (shares positions with primary)
};

typedef struct {
  uint32_t types[2], n, m, mask, flags,
           phd, sorted_n, iflags, dbid;
  const char *file;
} qmap_head_t;

typedef struct {
  idm_t idm;

  uint32_t *map;  	// id -> n
  const void **omap;	// n -> key
  uint32_t *key_hashes;	// n -> cached key hash
  void **table;		// n -> values
  size_t *key_sizes;	// n -> size of allocated key
  size_t *val_sizes;	// n -> size of allocated value
  qmap_blk_t *payload_bins[QMAP_POOL_BINS];

  ids_t linked;
  qmap_assoc_t *assoc;

  uint32_t *sorted_idx;
} qmap_t;

  static inline void *
qmap_payload_alloc(qmap_t *qmap, size_t key_len, size_t val_len)
{
  size_t raw = qmap_payload_off(key_len) + val_len;
  size_t size = (raw + (QMAP_POOL_STEP - 1)) & ~(QMAP_POOL_STEP - 1);
  qmap_blk_t *blk;
  uint32_t bin;

  if (size <= QMAP_POOL_MAX) {
    bin = (uint32_t) (size / QMAP_POOL_STEP - 1);
    blk = qmap->payload_bins[bin];
    if (blk) {
      qmap->payload_bins[bin] = blk->next;
      blk->next = NULL;
      blk->size = size;
      return (void *) (blk + 1);
    }
  } else {
    size = raw;
  }

  blk = malloc(sizeof(*blk) + size);
  CBUG(!blk, "malloc error (payload)\n");
  blk->next = NULL;
  blk->size = size;
  return (void *) (blk + 1);
}

  static inline void
qmap_payload_free(qmap_t *qmap, void *key)
{
  qmap_blk_t *blk;
  uint32_t bin;

  if (!key)
    return;

  blk = ((qmap_blk_t *) key) - 1;
  if (blk->size <= QMAP_POOL_MAX) {
    bin = (uint32_t) (blk->size / QMAP_POOL_STEP - 1);
    blk->next = qmap->payload_bins[bin];
    qmap->payload_bins[bin] = blk;
  } else
    free(blk);
}

  static inline void
qmap_payload_flush(qmap_t *qmap)
{
  for (size_t i = 0; i < QMAP_POOL_BINS; i++) {
    qmap_blk_t *blk = qmap->payload_bins[i];
    while (blk) {
      qmap_blk_t *next = blk->next;
      free(blk);
      blk = next;
    }
    qmap->payload_bins[i] = NULL;
  }
}

  static inline size_t
qmap_payload_cap(const void *key)
{
  const qmap_blk_t *blk = ((const qmap_blk_t *) key) - 1;
  return blk->size;
}

typedef struct {
  uint32_t hd, pos, sub_cur, ipos, end_pos, flags;
  size_t key_len;
  const void * key;
} qmap_cur_t;

typedef uint32_t qmap_hash_t(
    const void * const key,
    size_t len);

typedef struct {
  size_t len;
  qmap_measure_t *measure;
  qmap_hash_t *hash;
  qmap_cmp_t *cmp;
} qmap_type_t;

typedef struct {
  ids_t ids;
  int fd;
  char *mmaped;
  size_t size;
} qmap_file_t;

static qmap_head_t qmap_heads[QM_MAX];
static qmap_t qmaps[QM_MAX];
static qmap_cur_t qmap_cursors[QM_MAX];
static idm_t idm, cursor_idm;
static uint32_t _qsort_cmp_hd;

static qmap_type_t qmap_types[TYPES_MASK + 1];
static uint32_t types_n = 0;

static uint32_t qmap_files_hd, qmap_dbs_hd;
static int mdbs[QM_MAX];

/* }}} */

/* BUILT-INS {{{ */

  static uint32_t
qmap_nohash(const void * const key, size_t len UNUSED)
{
  uint32_t u;
  memcpy(&u, key, sizeof(u));
  return u;
}

static uint32_t
qmap_chash(const void *data, size_t len) {
  return XXH32(data, len, QM_SEED);
}

  static int
qmap_ccmp(const void * const a,
    const void * const b,
    size_t len)
{
  return memcmp((char *) a, (char *) b, len);
}

  static int
qmap_scmp(const void * const a,
    const void * const b,
    size_t len UNUSED)
{
  return strcmp((const char *)a, (const char *)b);
}

  static int
qmap_ucmp(const void * const a,
    const void * const b,
    size_t len UNUSED)
{
  uint32_t ua = * (const uint32_t *) a;
  uint32_t ub = * (const uint32_t *) b;
  if (ua < ub) return -1;
  if (ua > ub) return 1;
  return 0;
}

  static void
qmap_rassoc(const void **skey,
    const void * const pkey UNUSED,
    const void * const value)
{
  *skey = value;
}

/* }}} */

/* HELPER FUNCTIONS {{{ */

/* Easily obtain the pointer to the key */
  static inline void *
qmap_key(uint32_t hd, uint32_t n)
{
  qmap_t *qmap = &qmaps[hd];
  return (void *) qmap->omap[n];
}

/* Easily obtain the pointer to the value */
static inline void *
qmap_val(uint32_t hd, uint32_t n) {
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *pqmap;

  if (head->flags & QM_PGET)
    return qmap_key(head->phd, n);

  pqmap = &qmaps[head->phd];
  return * VAL_ADDR(pqmap, n);
}

/* In some cases we want to calculate the id based on the
 * qmap's hash function and the key, and the mask. Other
 * times it's not useful to do that. This is for when it is.
 *
 * When requested, also returns the computed key length/hash so
 * callers that need to store the metadata do not recompute it.
 */
static inline uint32_t
qmap_id_hash(uint32_t hd, const void * const key,
    size_t key_len, uint32_t key_hash);

  static inline uint32_t
qmap_id_ex(uint32_t hd, const void * const key,
    size_t *key_len_out, uint32_t *key_hash_out)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_type_t *type = &qmap_types[head->types[QM_KEY]];

  size_t key_len = type->measure
    ? type->measure(key)
    : type->len;
  uint32_t key_hash = type->hash(key, key_len);

  if (key_len_out)
    *key_len_out = key_len;
  if (key_hash_out)
    *key_hash_out = key_hash;

  return qmap_id_hash(hd, key, key_len, key_hash);
}

  static inline uint32_t
qmap_id_hash(uint32_t hd, const void * const key,
    size_t key_len, uint32_t key_hash)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  qmap_type_t *type = &qmap_types[head->types[QM_KEY]];
  uint32_t id = key_hash & head->mask;
  uint32_t probe_count = 0;

  while (1) {
    uint32_t n = qmap->map[id];

    if (n == QM_MISS)
      return id;

    const void *okey = qmap_key(hd, n);

    if (okey &&
        qmap->key_hashes[n] == key_hash)
    {
      size_t len;

      if (type->measure) {
        size_t okey_len =
          qmap->key_sizes[n];

        len = key_len > okey_len
          ? key_len
          : okey_len;
      } else {
        len = type->len;
      }

      if (type->cmp(okey, key, len) == 0)
        return id;
    }

    id = (id + 1) & head->mask;

    if (++probe_count >= head->m)
      return QM_MISS;
  }
}

  static inline uint32_t
qmap_id(uint32_t hd, const void * const key)
{
  return qmap_id_ex(hd, key, NULL, NULL);
}

/* }}} */

/* B-TREE SUPPORT HELPERS {{{ */

  static int
qmap_n_cmp(const void *a, const void *b)
{
  uint32_t n_a = *(const uint32_t *)a;
  uint32_t n_b = *(const uint32_t *)b;

  const void *key_a = qmap_key(_qsort_cmp_hd, n_a);
  const void *key_b = qmap_key(_qsort_cmp_hd, n_b);

  if (key_a == NULL || key_b == NULL)
    return 0;

  qmap_head_t *head = &qmap_heads[_qsort_cmp_hd];
  qmap_t *qmap = &qmaps[_qsort_cmp_hd];
  qmap_type_t *type = &qmap_types[head->types[QM_KEY]];

  if (type->measure) {
    size_t len_a = qmap->key_sizes[n_a];
    size_t len_b = qmap->key_sizes[n_b];
    size_t len = (len_a > len_b) ? len_a : len_b;
    return type->cmp(key_a, key_b, len);
  }

  return type->cmp(key_a, key_b, type->len);
}

  static void
qmap_rebuild_sorted(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  uint32_t n_idx = 0;

  for (uint32_t n = 0; n < qmap->idm.last; n++) {
    if (qmap->omap[n] != NULL)
      qmap->sorted_idx[n_idx++] = n;
  }
  head->sorted_n = n_idx;

  _qsort_cmp_hd = hd;
  qsort(qmap->sorted_idx, head->sorted_n,
      sizeof(uint32_t), qmap_n_cmp);

  head->iflags &= ~QM_SDIRTY;
}

/* Binary search modes */
enum {
  QMAP_BSEARCH_ANY = 0,    /* Find any match (original behavior) */
  QMAP_BSEARCH_FIRST = 1,  /* Find first occurrence */
  QMAP_BSEARCH_LAST = 2    /* Find last occurrence */
};

/* Unified binary search with mode parameter.
 * For QMAP_BSEARCH_ANY: returns insertion point if not found, sets *exact
 * For QMAP_BSEARCH_FIRST/LAST: returns position or -1 if not found */
  static int
qmap_bsearch_ex(uint32_t hd, const void *key, int *exact, int mode)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  int result = -1;

  if (head->iflags & QM_SDIRTY)
    qmap_rebuild_sorted(hd);

  if (head->sorted_n == 0) {
    if (exact) *exact = 0;
    return (mode == QMAP_BSEARCH_ANY) ? 0 : -1;
  }

  qmap_type_t *type = &qmap_types[head->types[QM_KEY]];
  size_t key_len = qmap_len(head->types[QM_KEY], key);
  int low = 0, high = (int) head->sorted_n - 1;
  int mid = 0;

  if (exact) *exact = 0;

  while (low <= high) {
    mid = low + (high - low) / 2;
    const void *mid_key = qmap_key(hd, qmap->sorted_idx[mid]);

    size_t len;
    if (type->measure) {
      size_t mid_len = qmap->key_sizes[qmap->sorted_idx[mid]];
      len = (key_len > mid_len) ? key_len : mid_len;
    } else
      len = type->len;

    int cmp = type->cmp(mid_key, key, len);

    if (cmp == 0) {
      if (exact) *exact = 1;
      result = mid;
      if (mode == QMAP_BSEARCH_ANY)
        return mid;  /* Return immediately for ANY mode */
      else if (mode == QMAP_BSEARCH_FIRST)
        high = mid - 1;  /* Continue searching left */
      else /* QMAP_BSEARCH_LAST */
        low = mid + 1;   /* Continue searching right */
    } else if (cmp < 0)
      low = mid + 1;
    else
      high = mid - 1;
  }

  return (mode == QMAP_BSEARCH_ANY && (exact == NULL || !*exact)) ? low : result;
}

/* Wrapper for backward compatibility with original qmap_bsearch */
  static inline int
qmap_bsearch(uint32_t hd, const void *key, int *exact)
{
  return qmap_bsearch_ex(hd, key, exact, QMAP_BSEARCH_ANY);
}

/* OPEN / INITIALIZATION {{{ */

/* Low level way of opening databases. */
  static uint32_t
_qmap_open(uint32_t ktype, uint32_t vtype,
    uint32_t mask, uint32_t flags)
{
  uint32_t hd = idm_new(&idm);
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  uint32_t len;
  size_t ids_len;

  mask = mask ? mask : QM_DEFAULT_MASK;

  /* QM_MULTIVALUE requires QM_SORTED */
  if ((flags & QM_MULTIVALUE) && !(flags & QM_SORTED)) {
    fprintf(stderr, "qmap: QM_MULTIVALUE requires QM_SORTED flag\n");
    idm_del(&idm, hd);
    return QM_MISS;
  }

  DEBUG(1, "%u %u 0x%x %u\n",
      hd, ktype,
      mask, flags);

  len = mask + 1u;

  CBUG((len & mask) != 0, "mask must be 2^k - 1\n");
  ids_len = len * sizeof(uint32_t);

  qmap->map = malloc(ids_len);
  qmap->omap = malloc(len * sizeof(void *));
  CBUG(!(qmap->map && qmap->omap), "malloc error\n");
  qmap->idm = idm_init();
  qmap->linked = ids_init();

  head->m = len;
  head->types[QM_KEY] = ktype;
  head->types[QM_VALUE] = vtype;
  head->mask = mask;
  head->flags = flags;
  head->phd = hd;

  // STORE {{{
  qmap->table = malloc(sizeof(void *) * len);
  CBUG(!qmap->table, "malloc error (table)\n");
  memset(qmap->table, 0, sizeof(void *) * len);
  // }}}

  qmap->key_hashes = calloc(len, sizeof(*qmap->key_hashes));
  CBUG(!qmap->key_hashes, "malloc error (key_hashes)\n");

  if (flags & QM_SORTED) {
    qmap->sorted_idx = malloc(sizeof(uint32_t) * len);
    CBUG(!qmap->sorted_idx, "malloc error (sorted_idx)\n");
  } else
    qmap->sorted_idx = NULL;

  qmap->key_sizes = calloc(len, sizeof(*qmap->key_sizes));
  qmap->val_sizes = calloc(len, sizeof(*qmap->val_sizes));
  CBUG(!(qmap->key_sizes && qmap->val_sizes), "malloc error (size arrays)\n");

  head->iflags |= QM_SDIRTY;
  head->sorted_n = 0;

  memset(qmap->map, 0xFF, ids_len);
  memset(qmap->omap, 0, sizeof(void *) * len);

  return hd;
}

static inline void
qmap_load_file(char *filename, uint32_t dbid);

static inline uint32_t
_qmap_put(uint32_t hd, const void * key,
    const void *value, uint32_t pn);

  static void
qmap_rebuild_map(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];

  memset(qmap->map, 0xFF,
      sizeof(uint32_t) * head->m);

  for (uint32_t n = 0; n < qmap->idm.last; n++) {
    const void *key = qmap->omap[n];

    if (!key)
      continue;

    uint32_t id =
      qmap->key_hashes[n] & head->mask;

    while (qmap->map[id] != QM_MISS)
      id = (id + 1) & head->mask;

    qmap->map[id] = n;
  }
}

  static void
qmap_grow(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  uint32_t old_m = head->m;
  uint32_t new_m = old_m << 1;

  CBUG((new_m & (new_m - 1)) != 0,
      "qmap_grow: capacity not power-of-two");

  void *tmp;

  tmp = realloc(qmap->omap, sizeof(void *) * new_m);
  CBUG(!tmp, "realloc(omap)");
  qmap->omap = tmp;
  memset(&qmap->omap[old_m], 0,
      sizeof(void *) * (new_m - old_m));

  if (qmap->table) {
    tmp = realloc(qmap->table,
        sizeof(void *) * new_m);

    CBUG(!tmp, "realloc(table)");

    qmap->table = tmp;

    memset(&qmap->table[old_m], 0,
        sizeof(void *) * (new_m - old_m));
  }

  tmp = realloc(qmap->key_hashes, sizeof(uint32_t) * new_m);
  CBUG(!tmp, "realloc(key_hashes)");
  qmap->key_hashes = tmp;
  memset(qmap->key_hashes + old_m, 0,
      sizeof(uint32_t) * (new_m - old_m));

  tmp = realloc(qmap->key_sizes, sizeof(size_t) * new_m);
  CBUG(!tmp, "realloc(key_sizes)");
  qmap->key_sizes = tmp;
  memset(qmap->key_sizes + old_m, 0,
      sizeof(size_t) * (new_m - old_m));

  tmp = realloc(qmap->val_sizes, sizeof(size_t) * new_m);
  CBUG(!tmp, "realloc(val_sizes)");
  qmap->val_sizes = tmp;
  memset(qmap->val_sizes + old_m, 0,
      sizeof(size_t) * (new_m - old_m));

  if (qmap->sorted_idx) {
    tmp = realloc(qmap->sorted_idx, sizeof(uint32_t) * new_m);
    CBUG(!tmp, "realloc(sorted_idx)");
    qmap->sorted_idx = tmp;
    memset(qmap->sorted_idx + old_m, 0xFF,
        sizeof(uint32_t) * (new_m - old_m));
  }

  free(qmap->map);
  qmap->map = malloc(sizeof(uint32_t) * new_m);
  CBUG(!qmap->map, "malloc(map)");
  memset(qmap->map, 0xFF, sizeof(uint32_t) * new_m);

  head->m = new_m;
  head->mask = new_m - 1;

  CBUG(head->m != head->mask + 1,
      "qmap invariant broken");

  qmap_rebuild_map(hd);
}

  uint32_t /* API */
qmap_open(const char *filename,
    const char *database,
    uint32_t ktype, uint32_t vtype,
    uint32_t mask, uint32_t flags)
{
  uint32_t hd = _qmap_open(ktype, vtype, mask, flags);

  /* Check if open failed */
  if (hd == QM_MISS)
    return QM_MISS;

  qmap_head_t *head = &qmap_heads[hd];

  head->file = filename;
  if (database)
    head->dbid = XXH32(database, strlen(database), QM_SEED);
  else
    head->dbid = QM_MISS;

  if (!filename)
    goto file_skip;

  if (database) {
    char buf[strlen(filename)
      + strlen(database) + 2];

    snprintf(buf, sizeof(buf), "%s/%s",
        filename, database);

    const uint32_t *ehd = qmap_get(qmap_dbs_hd, buf);
    uint32_t old_hd = ehd ? *ehd : QM_MISS;
    qmap_put(qmap_dbs_hd, buf, &hd);

    if (old_hd != QM_MISS && mdbs[old_hd])
      mdbs[old_hd] = 0;
  }

  mdbs[hd] = 1;  /* Mark as dirty for save, regardless of database name */

  const qmap_file_t *file_p
    = qmap_get(qmap_files_hd, filename);

  if (!file_p) {
    qmap_file_t file;
    memset(&file, 0, sizeof(file));
    file.ids = ids_init();
    file.fd = -1;
    ids_push(&file.ids, hd);
    qmap_put(qmap_files_hd, filename, &file);
  } else
    ids_push((ids_t *) &file_p->ids, hd);

file_skip:
  if (filename)
    qmap_load_file((char*) filename, head->dbid);

  if (!(flags & QM_MIRROR))
    return hd;

  flags &= ~QM_AINDEX;
  uint32_t mirror_hd = _qmap_open(vtype, ktype, mask, flags | QM_PGET);
  qmap_heads[mirror_hd].iflags |= QM_IS_MIRROR;  /* Mark as mirror for position sharing */
  qmap_assoc(hd + 1, hd, NULL);

  /* If data was loaded before mirror creation, populate the mirror now */
  if (filename && head->n > 0) {
    uint32_t cur = qmap_iter(hd, NULL, 0);
    const void *key, *value;
    while (qmap_next(&key, &value, cur)) {
      _qmap_put(mirror_hd, value, key, qmaps[hd].map[qmap_id(hd, key)]);
    }
    qmap_fin(cur);
  }

  return hd;
}

  static size_t
s_measure(const void *key)
{
  return strlen(key) + 1;
}

static void file_close(qmap_file_t *file) {
  if (file->mmaped) {
    CBUG(munmap(file->mmaped, file->size) == -1,
        "munmap failed");
    file->mmaped = 0;
  }

  if (file->fd >= 0)
    close(file->fd);
  file->fd = -1;
}

__attribute__((destructor))
  static void qmap_destruct(void) {
    qmap_save();

    for (uint32_t i = idm.last; i-- > 0; )
      qmap_close(i);

    idm_drop(&cursor_idm);
    idm_drop(&idm);

    uint32_t cur = qmap_iter(qmap_files_hd, NULL, 0);
    const void *key, *value;

    while (qmap_next(&key, &value, cur))
      file_close((qmap_file_t *) value);

    qmap_close(qmap_dbs_hd);
    qmap_close(qmap_files_hd);
  }

__attribute__((constructor))
  static void
qmap_init(void)
{
  qmap_type_t *type;

  memset(qmaps, 0, sizeof(qmaps));
  memset(qmap_heads, 0, sizeof(qmap_heads));

  idm = idm_init();
  cursor_idm = idm_init();

  // QM_PTR
  type = &qmap_types[qmap_reg(sizeof(void *))];

  // QM_HNDL
  type = &qmap_types[qmap_reg(sizeof(uint32_t))];
  type->hash = qmap_nohash;
  type->cmp = qmap_ucmp;

  // QM_STR
  type = &qmap_types[qmap_mreg(s_measure)];
  type->cmp = qmap_scmp;

  // QM_U32
  type = &qmap_types[qmap_reg(sizeof(uint32_t))];
  type->cmp = qmap_ucmp;

  uint32_t qm_file = qmap_reg(sizeof(qmap_file_t));
  qmap_files_hd = _qmap_open(QM_STR, qm_file,
      QM_DEFAULT_MASK, 0);

  qmap_dbs_hd = _qmap_open(QM_STR, QM_U32, QM_DEFAULT_MASK, 0);
}

/* }}} */

/* PUT {{{ */

/* This is the low-level put. It doesn't aim to provide
 * MIRROR functionality in itself, just putting in whatever
 * kind of map.
 */
/* Helper: Update IDM last position if needed */
  static inline void
update_idm_last(qmap_t *qmap, uint32_t pn)
{
  if (pn >= qmap->idm.last)
    qmap->idm.last = pn + 1;
}

  static inline uint32_t
_qmap_put(uint32_t hd, const void * key,
    const void *value, uint32_t pn)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];

  /* Grow before clustering gets pathological */
  if (!(head->flags & QM_NOGROW)) {
    if ((head->n + 1) * 4 >= head->m * 3)
      qmap_grow(hd);
  }

  uint32_t n;
  const void *aval = value;
  void *rval, *rkey;
  size_t key_len, klen;
  uint32_t key_hash;
  uint32_t lookup_id;
  uint32_t key_id;

  if (key) {
    uint32_t old_n;

    lookup_id = qmap_id_ex(hd, key, &key_len, &key_hash);
    if (lookup_id == QM_MISS) {
      WARN("qmap %u: probe failure after grow", hd);
      return QM_MISS;
    }
    old_n = qmap->map[lookup_id];

    if (old_n == QM_MISS) {
      if (pn != QM_MISS) {
        n = pn;
        /* Update IDM to know about this position */
        update_idm_last(qmap, pn);
      } else {
        n = idm_new(&qmap->idm);
      }
      head->n ++;
    } else if (head->flags & QM_MULTIVALUE) {
      /* QM_MULTIVALUE: Allow duplicate keys.
       * If pn is provided (from qmap_assoc), use it.
       * Otherwise, allocate a new position.
       * Don't update hash table - it keeps pointing to first occurrence.
       * Duplicate is accessible via sorted_idx iteration. */
      if (pn != QM_MISS && pn != old_n) {
        n = pn;
        /* Update IDM to know about this position */
        update_idm_last(qmap, pn);
      } else if (pn == QM_MISS) {
        n = idm_new(&qmap->idm);
      } else {
        n = old_n;  /* pn == old_n, update in place */
      }

      if (n != old_n)
        head->n ++;
    } else
      n = old_n;
  } else {
    key_id = n = idm_new(&qmap->idm);
    head->n ++;
    key = &key_id;
    lookup_id = qmap_id_ex(hd, key, &key_len, &key_hash);
  }

  if (n >= head->m) {
    if (head->flags & QM_NOGROW) {
      head->n--;
      WARN("qmap %u: capacity reached (%u entries, max %u)",
          hd, head->n, head->m);
      return QM_MISS;
    }
    qmap_grow(hd);
    lookup_id = qmap_id_hash(hd, key, key_len, key_hash);
  }
  DEBUG(2, "%u %u %u %p\n", hd, n, lookup_id, key);

  rkey = (void *) key;
  qmap->key_hashes[n] = key_hash;

  if (head->phd == hd) {
    if (head->types[QM_VALUE] == QM_PTR)
      value = &value;

    klen = qmap_len(head->types[QM_VALUE], aval);

    if (qmap->map[lookup_id] == n) {
      const void *old_key = qmap_key(hd, n);
      size_t off = qmap_payload_off(key_len);
      size_t need = qmap_payload_off(key_len) + klen;

      /* Reuse key allocation if key/value fit in the existing block. */
      if (qmap->key_sizes[n] == key_len &&
          memcmp(old_key, key, key_len) == 0 &&
          qmap_payload_cap(old_key) >= need) {
        rkey = (void *) old_key;
        rval = (void *) ((char *) rkey + off);
      } else {
        qmap_payload_free(qmap, (void *) old_key);
        rkey = qmap_payload_alloc(qmap, key_len, klen);
        rval = (void *) ((char *) rkey + off);
      }

      memcpy(rkey, key, key_len);
      memcpy(rval, value, klen);
      qmap->key_sizes[n] = key_len;
      qmap->val_sizes[n] = klen;
    } else {
      /* New entry - allocate fresh */
      size_t off = qmap_payload_off(key_len);
      rkey = qmap_payload_alloc(qmap, key_len, klen);
      rval = (void *) ((char *) rkey + off);
      memcpy(rkey, key, key_len);
      memcpy(rval, value, klen);
      qmap->key_sizes[n] = key_len;
      qmap->val_sizes[n] = klen;
    }

    * VAL_ADDR(qmap, n) = rval;
  }

  qmap->omap[n] = rkey;

  /* For QM_MULTIVALUE duplicates, don't update hash table */
  if (!(head->flags & QM_MULTIVALUE) || qmap->map[lookup_id] == QM_MISS || qmap->map[lookup_id] == n)
    qmap->map[lookup_id] = n;

  head->iflags |= QM_SDIRTY;

  return lookup_id;
}

  uint32_t /* API */
qmap_put(uint32_t hd, const void * const key,
    const void * const value)
{
  uint32_t ahd, n, id;
  idsi_t *cur;
  const void *rkey, *rval;

  id = _qmap_put(hd, key, value, QM_MISS);
  if (id == QM_MISS)
    return QM_MISS;
  n = qmaps[hd].map[id];

  cur = ids_iter(&qmaps[hd].linked);
  rkey = qmap_key(hd, n);
  rval = qmap_val(hd, n);

  while (ids_next(&ahd, &cur)) {
    qmap_t *aqmap;
    const void *skey;
    qmap_head_t *ahead;

    aqmap = &qmaps[ahd];
    ahead = &qmap_heads[ahd];
    aqmap->assoc(&skey, rkey, rval);

    /* Only share positions with QM_MIRROR maps.
     * General secondary indexes (qmap_assoc) get independent positions. */
    if (ahead->iflags & QM_IS_MIRROR) {
      _qmap_put(ahd, skey, rval, n);  /* Mirror: share position */
    } else {
      _qmap_put(ahd, skey, rval, QM_MISS);  /* Secondary index: independent */
    }
  }

  return id;
}

/* }}} */

/* GET {{{ */

static int qmap_lnext(uint32_t *sn, uint32_t cur_id);

  const void * /* API */
qmap_get(uint32_t hd, const void * const key)
{
  uint32_t cur_id = qmap_iter(hd, key, 0), sn;

  if (!qmap_lnext(&sn, cur_id))
    return NULL;

  qmap_fin(cur_id);

  return qmap_val(hd, sn);
}

/* }}} */

/* DELETE {{{ */

  static inline uint32_t
qmap_root(uint32_t hd)
{
  while(qmap_heads[hd].phd != hd)
    hd = qmap_heads[hd].phd;

  return hd;
}

static void qmap_ndel_topdown(uint32_t hd, uint32_t n) {
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  const void *key;
  uint32_t id, ahd;
  idsi_t *cur;

  // Guard against already-closed maps (omap is NULL after close)
  if (!qmap->omap)
    return;

  if (n >= head->m)
    return;

  cur = ids_iter(&qmap->linked);

  while (ids_next(&ahd, &cur))
    qmap_ndel_topdown(ahd, n);

  key = qmap_key(hd, n);

  if (!key) {
    qmap->omap[n] = NULL;
    idm_del(&qmap->idm, n);
    head->n --;
    return;
  }

  id = qmap_id(hd, key);

  /* For QM_MULTIVALUE maps, check if other duplicates exist before clearing hash entry.
   * Do this BEFORE freeing the key! */
  uint32_t new_map_entry = QM_MISS;
  if (head->flags & QM_MULTIVALUE) {
    int first = qmap_bsearch_ex(hd, key, NULL, QMAP_BSEARCH_FIRST);

    if (first != -1) {
      uint32_t first_pos = qmap->sorted_idx[first];
      if (first_pos == n) {
        /* Deleting the first entry, check if a second exists */
        int second = first + 1;
        if (second < (int)head->sorted_n) {
          const void *second_key = qmap_key(hd, qmap->sorted_idx[second]);
          qmap_type_t *type = &qmap_types[head->types[QM_KEY]];
          size_t key_len = qmap->key_sizes[n];
          size_t len;
          if (type->measure) {
            size_t second_len = qmap->key_sizes[qmap->sorted_idx[second]];
            len = (key_len > second_len) ? key_len : second_len;
          } else
            len = type->len;

          if (type->cmp(key, second_key, len) == 0)
            new_map_entry = qmap->sorted_idx[second];
        }
      } else {
        /* Not deleting first entry, first remains valid */
        new_map_entry = first_pos;
      }
    }
  }

  if (head->phd == hd) {
    qmap_payload_free(qmap, (void *) key);
    * VAL_ADDR(qmap, n) = NULL;
  }

  qmap->key_hashes[n] = 0;
  qmap->key_sizes[n] = 0;
  qmap->val_sizes[n] = 0;

  head->iflags |= QM_SDIRTY;

  /* Update hash table entry */
  if (id != QM_MISS) {
    if (new_map_entry != QM_MISS)
      qmap->map[id] = new_map_entry;
    else
      qmap->map[id] = QM_MISS;
  }

  qmap->omap[n] = NULL;
  idm_del(&qmap->idm, n);
  head->n --;

}

/* Delete based on position */
static inline void
qmap_ndel(uint32_t hd, uint32_t n) {
  qmap_ndel_topdown(qmap_root(hd), n);
}

  static void
qmap_clear_fast(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];

  if (head->phd == hd) {
    for (uint32_t n = 0; n < qmap->idm.last; n++) {
      const void *key = qmap->omap[n];
      if (!key)
        continue;
      qmap_payload_free(qmap, (void *) key);
    }
  }

  memset(qmap->map, 0xFF, sizeof(uint32_t) * head->m);
  memset(qmap->omap, 0, sizeof(void *) * head->m);
  memset(qmap->key_hashes, 0, sizeof(uint32_t) * head->m);
  memset(qmap->key_sizes, 0, sizeof(size_t) * head->m);
  if (head->phd == hd) {
    memset(qmap->table, 0, sizeof(void *) * head->m);
    memset(qmap->val_sizes, 0, sizeof(size_t) * head->m);
  }

  idm_drop(&qmap->idm);
  qmap->idm.last = 0;
  head->n = 0;
  head->iflags |= QM_SDIRTY;
}

  void /* API */
qmap_del(uint32_t hd, const void * const key)
{
  qmap_head_t *head = &qmap_heads[hd];
  uint32_t cur = qmap_iter(hd, key, 0), sn;

  if (head->flags & QM_MULTIVALUE) {
    /* For QM_MULTIVALUE maps, only delete the first occurrence */
    if (qmap_lnext(&sn, cur))
      qmap_ndel(hd, sn);
    qmap_fin(cur);
  } else {
    /* For regular maps, delete all matching entries */
    while (qmap_lnext(&sn, cur))
      qmap_ndel(hd, sn);
  }
}

  void
qmap_del_all(uint32_t hd, const void * const key)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];

  if (head->flags & QM_MULTIVALUE) {
    /* Fast path: if nothing is linked to this map, bulk-delete by
     * clearing the matching slots once and rebuilding the hash table.
     * This avoids repeated probe-chain maintenance for every duplicate. */
    uint32_t cur = qmap_get_multi(hd, key);
    uint32_t sn;
    size_t n = 0, cap = head->n ? head->n : 1;
    uint32_t *positions = malloc(sizeof(*positions) * cap);
    int fast_path = ids_iter(&qmap->linked) == NULL && head->phd == hd;

    if (cur == QM_MISS)
      return;

    CBUG(!positions, "malloc error (del_all)\n");

    while (qmap_lnext(&sn, cur))
      positions[n++] = sn;

    if (fast_path) {
      for (size_t i = 0; i < n; i++) {
        uint32_t pos = positions[i];
        const void *old_key = qmap_key(hd, pos);

        qmap_payload_free(qmap, (void *) old_key);
        qmap->key_sizes[pos] = 0;
        qmap->val_sizes[pos] = 0;
        qmap->omap[pos] = NULL;
        * VAL_ADDR(qmap, pos) = NULL;
        idm_del(&qmap->idm, pos);
        head->n--;
      }

      head->iflags |= QM_SDIRTY;

      if (head->n == 0)
        memset(qmap->map, 0xFF, sizeof(uint32_t) * head->m);
      else
        qmap_rebuild_map(hd);
    } else {
      for (size_t i = 0; i < n; i++)
        qmap_ndel(hd, positions[i]);
    }

    free(positions);
  } else {
    /* For regular maps, just call qmap_del once */
    qmap_del(hd, key);
  }
}

/* }}} */

/* ITERATION {{{ */

  void /* API */
qmap_fin(uint32_t cur_id)
{
  qmap_cur_t *cursor = &qmap_cursors[cur_id];

  if (cursor->sub_cur)
    qmap_fin(cursor->sub_cur);

  idm_del(&cursor_idm, cur_id);
}

  uint32_t /* API */
qmap_iter(uint32_t hd, const void * const key, uint32_t flags)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  uint32_t cur_id = idm_new(&cursor_idm);
  qmap_cur_t *cursor = &qmap_cursors[cur_id];

  if (key && (head->flags & QM_MULTIVALUE)) {
    /* For QM_MULTIVALUE maps, use sorted iteration to find all duplicates.
     * Find first occurrence of this key */
    int first = qmap_bsearch_ex(hd, key, NULL, QMAP_BSEARCH_FIRST);
    cursor->pos = (first != -1) ? (uint32_t)first : head->sorted_n;
    cursor->end_pos = QM_MISS;
    /* Use sorted iteration but stop at key boundary */
    flags |= QM_RANGE;
  } else if (key && (flags & QM_RANGE) && (head->flags & QM_SORTED)) {
    int exact;
    cursor->pos = qmap_bsearch(hd, key, &exact);
    cursor->end_pos = head->sorted_n;
  } else if (key && !(flags & QM_RANGE)) {
    uint32_t id = qmap_id(hd, key);
    if (id == QM_MISS) {
      cursor->pos = QM_MISS;
      cursor->end_pos = QM_MISS;
    } else {
      cursor->pos = qmap->map[id];
      cursor->end_pos = cursor->pos;
    }
  } else
    cursor->pos = cursor->end_pos = 0;

  cursor->ipos = cursor->pos;
  cursor->sub_cur = 0;
  cursor->hd = hd;
  cursor->key = key;
  cursor->key_len = key ? qmap_len(head->types[QM_KEY], key) : 0;
  cursor->flags = flags;
  return cur_id;
}

/* low-level next */
  static int
qmap_lnext(uint32_t *sn, uint32_t cur_id)
{
  register qmap_cur_t *cursor
    = &qmap_cursors[cur_id];
  register qmap_head_t *head = &qmap_heads[cursor->hd];
  register qmap_t *qmap = &qmaps[cursor->hd];
  uint32_t n;
  const void *key;

  if ((cursor->flags & QM_RANGE)
      && (head->flags & QM_SORTED))
  {
    if (head->iflags & QM_SDIRTY)
      qmap_rebuild_sorted(cursor->hd);

    if (cursor->pos >= head->sorted_n)
      goto end;

    n = qmap->sorted_idx[cursor->pos];

    if (cursor->key && (head->flags & QM_MULTIVALUE)) {
      if (cursor->end_pos == QM_MISS)
        cursor->end_pos = (uint32_t) qmap_bsearch_ex(
            cursor->hd, cursor->key, NULL,
            QMAP_BSEARCH_LAST);
      if (cursor->pos > cursor->end_pos)
        goto end;
    }

    *sn = n;
    cursor->pos++;
    return 1;
  }

cagain:
  n = cursor->pos;

  if (n >= qmap->idm.last)
    goto end;

  key = qmap_key(cursor->hd, n);
  if (key == NULL) {
    cursor->pos++;
    goto cagain;
  }

  if (cursor->flags & QM_RANGE) {
    if (!cursor->key)
      goto next;

    qmap_type_t *type = &qmap_types[head->types[QM_KEY]];
    size_t len;

    if (type->measure) {
      size_t key_len = type->measure(key);
      size_t start_key_len = cursor->key_len;
      len = (key_len > start_key_len)
        ? key_len
        : start_key_len;
    } else
      len = type->len;

    if (type->cmp(key, cursor->key, len) < 0) {
      cursor->pos++;
      goto cagain;
    }
  } else if (cursor->key && n != cursor->ipos)
    goto end;
next:

  DEBUG(3, "NEXT! cur_id %u key %p\n",
      cur_id, key);

  cursor->pos++;
  *sn = n;
  return 1;
end:
  idm_del(&cursor_idm, cur_id);
  *sn = QM_MISS;
  return 0;
}

  int /* API */
qmap_next(const void ** ckey, const void ** cval,
    uint32_t cur_id)
{
  register qmap_cur_t *c;
  uint32_t sn;
  int ret = qmap_lnext(&sn, cur_id);

  if (!ret)
    return 0;

  c = &qmap_cursors[cur_id];
  *ckey = qmap_key(c->hd, sn);
  *cval = qmap_val(c->hd, sn);
  return 1;
}

/* }}} */

/* DROP + CLOSE + OTHERS {{{ */

  void /* API */
qmap_drop(uint32_t hd)
{
  qmap_t *qmap = &qmaps[hd];

  if (ids_iter(&qmap->linked) == NULL) {
    qmap_clear_fast(hd);
    return;
  }

  uint32_t cur_id = qmap_iter(hd, NULL, 0), sn;

  while (qmap_lnext(&sn, cur_id))
    qmap_ndel(hd, sn);
}

  void /* API */
qmap_close(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  qmap_t *qmap = &qmaps[hd];
  idsi_t *cur;
  uint32_t ahd;

  if (!qmap->omap)
    return;

  qmap_drop(hd);

  cur = ids_iter(&qmap->linked);
  while (ids_next(&ahd, &cur))
    qmap_close(ahd);

  ids_drop(&qmap->linked);
  idm_drop(&qmap->idm);
  qmap->idm.last = 0;
  qmap_payload_flush(qmap);
  free(qmap->map);
  free(qmap->omap);
  free(qmap->key_hashes);
  free(qmap->key_sizes);
  free(qmap->val_sizes);
  if (qmap->sorted_idx)
    free(qmap->sorted_idx);
  if (qmap_heads[hd].phd == hd)
    free(qmap->table);
  qmap->omap = NULL;
  idm_del(&idm, hd);

  // remove any file associations so we don't try
  // saving it to a file after it is closed.
  if (!head->file)
    return;
  const qmap_file_t *file = qmap_get(qmap_files_hd, head->file);
  if (!file)
    return;
  ids_remove((ids_t *) &file->ids, hd);

}

  void /* API */
qmap_assoc(uint32_t hd, uint32_t link, qmap_assoc_t cb)
{
  qmap_t *qmap = &qmaps[hd];

  if (!cb)
    cb = qmap_rassoc;

  ids_push(&qmaps[link].linked, hd);

  qmap->assoc = cb;
  qmap_heads[hd].phd = link;

  free(qmap->table);
  qmap->table = NULL;
}

  uint32_t /* API */
qmap_reg(size_t len)
{
  if (types_n > TYPES_MASK) {
    fprintf(stderr, "qmap_reg: type limit reached\n");
    return QM_MISS;
  }
  uint32_t id = types_n ++;
  qmap_type_t *type = &qmap_types[id];

  memset(type, 0, sizeof(qmap_type_t));
  type->len = len;
  type->hash = qmap_chash;
  type->cmp = qmap_ccmp;
  return id;
}

  void
qmap_cmp_set(uint32_t ref, qmap_cmp_t *cmp)
{
  qmap_type_t *type = &qmap_types[ref];
  type->cmp = cmp;
}

  uint32_t /* API */
qmap_mreg(qmap_measure_t *measure)
{
  if (types_n > TYPES_MASK) {
    fprintf(stderr, "qmap_mreg: type limit reached\n");
    return QM_MISS;
  }
  uint32_t id = types_n ++;
  qmap_type_t *type = &qmap_types[id];

  memset(type, 0, sizeof(qmap_type_t));
  type->measure = measure;
  type->hash = qmap_chash;
  type->cmp = qmap_ccmp;
  type->len = 0;
  return id;
}

  size_t /* API */
qmap_len(uint32_t type_id, const void *key)
{
  qmap_type_t *type = &qmap_types[type_id];

  return type->measure
    ? type->measure(key)
    : type->len;
}

/* }}} */

  inline static size_t
_qmap_load(uint32_t hd, const char *mmaped, uint32_t dbid)
{
  const char *mm = mmaped;
  const char *mm_start = mmaped;

  uint32_t lid = * (uint32_t *) mm;
  mm += sizeof(uint32_t);

  size_t size = * (size_t *) mm;
  mm += sizeof(size_t);

  if (dbid != QM_MISS && lid != dbid)
    return mm + size + sizeof(uint32_t) - mm_start;

  uint32_t amount = * (uint32_t*) mm;
  mm += sizeof(uint32_t);

  qmap_head_t *head = &qmap_heads[hd];
  uint32_t ktype = head->types[QM_KEY];
  uint32_t vtype = head->types[QM_VALUE];

  for (uint32_t i = 0; i < amount; i++) {
    size_t klen = qmap_len(ktype, mm);
    const char *mval = mm + klen;
    size_t vlen = qmap_len(vtype, mval);

    qmap_put(hd, mm, mval);
    mm = mval + vlen;
  }

  head->iflags |= QM_SDIRTY;
  return mm - mm_start;
}

  static inline void
qmap_load_file(char *filename, uint32_t dbid)
{
  qmap_file_t *file = (qmap_file_t *)
    qmap_get(qmap_files_hd, filename);

  struct stat sb;
  char *mm;
  idsi_t *cur;
  uint32_t hd;

  if (file->mmaped)
    goto skip_open;

  file->fd = open(filename, O_RDONLY);
  if (file->fd < 0)
    return;

  CBUG(fstat(file->fd, &sb) == -1, "fstat");

  file->size = sb.st_size;
  if (file->size == 0) {
    close(file->fd);
    file->fd = -1;
    return;
  }

  file->mmaped = (char*) mmap(NULL, file->size,
      PROT_READ, MAP_SHARED, file->fd, 0);
  if (file->mmaped == MAP_FAILED) {
    file->mmaped = 0;
    close(file->fd);
    file->fd = -1;
    return;
  }

skip_open:
  cur = (idsi_t *) ids_iter(&file->ids);

  mm = file->mmaped;
  while (ids_next(&hd, &cur))
    mm += _qmap_load(hd, mm, dbid);
}

  static size_t
_qmap_calc_size(uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  uint32_t ktype = head->types[QM_KEY];
  uint32_t vtype = head->types[QM_VALUE];
  size_t total_size = sizeof(uint32_t) + sizeof(size_t) + sizeof(head->n);
  uint32_t cur = qmap_iter(hd, NULL, 0);
  const void *key, *value;

  while (qmap_next(&key, &value, cur)) {
    total_size += qmap_len(ktype, key);
    total_size += qmap_len(vtype, value);
  }

  qmap_fin(cur);
  return total_size;
}

  static inline size_t
qmap_calc_file_size(const ids_t *hds)
{
  size_t size = 0;
  idsi_t *cur = (idsi_t *) ids_iter((ids_t *) hds);
  uint32_t hd;

  while (ids_next(&hd, &cur))
    if (mdbs[hd])
      size += _qmap_calc_size(hd);

  return size;
}

  inline static size_t
_qmap_save(void *mmaped, uint32_t hd)
{
  qmap_head_t *head = &qmap_heads[hd];
  uint32_t ktype = head->types[QM_KEY];
  uint32_t vtype = head->types[QM_VALUE];
  char *mm_start = mmaped;
  char *mm = mmaped;
  uint32_t cur = qmap_iter(hd, NULL, 0);
  const void *key, *value;

  memcpy(mm, &head->dbid, sizeof(head->dbid));
  mm += sizeof(head->dbid);

  size_t size = _qmap_calc_size(hd);
  memcpy(mm, &size, sizeof(size));
  mm += sizeof(size);

  memcpy(mm, &head->n, sizeof(head->n));
  mm += sizeof(head->n);

  while (qmap_next(&key, &value, cur)) {
    size_t klen = qmap_len(ktype, key);
    size_t vlen = qmap_len(vtype, value);

    memcpy(mm, key, klen);
    mm += klen;
    memcpy(mm, value, vlen);
    mm += vlen;
  }

  qmap_fin(cur);
  return mm - mm_start;
}

  static inline void
qmap_save_file(char *filename)
{
  qmap_file_t *file = (qmap_file_t *) qmap_get(qmap_files_hd, filename);
  CBUG(!file, "called with unknown filename");

  if (file->mmaped)
    file_close(file);

  file->size = qmap_calc_file_size(&file->ids);

  file->fd = open(filename, O_RDWR | O_CREAT,
      S_IRUSR | S_IWUSR);

  CBUG(file->fd == -1, "open for save failed");

  CBUG(ftruncate(file->fd, (off_t) file->size) == -1,
      "ftruncate failed");

  if (file->size == 0) {
    close(file->fd);
    file->fd = -1;
    return;
  }

  file->mmaped = (char*) mmap(NULL, file->size,
      PROT_WRITE, MAP_SHARED, file->fd, 0);
  CBUG(file->mmaped == MAP_FAILED, "mmap for save failed");

  char *mm = file->mmaped;
  uint32_t hd;

  idsi_t *idsi = ids_iter(&file->ids);

  while (ids_next(&hd, &idsi)) {
    if (!mdbs[hd])
      continue;

    size_t size_written = _qmap_save(mm, hd);
    mm += size_written;
  }

  file_close(file);
}

  void /* API */
qmap_save(void)
{
  uint32_t c = qmap_iter(qmap_files_hd, NULL, 0);
  const void *key, *value;

  while (qmap_next(&key, &value, c))
    qmap_save_file((char *) key);
}

/* MULTI-VALUE API {{{ */

  uint32_t /* API */
qmap_get_multi(uint32_t hd, const void *key)
{
  qmap_head_t *head = &qmap_heads[hd];
  uint32_t cur = qmap_iter(hd, key, 0);

  if (!key)
    return cur;

  /* qmap_iter() already did the lookup. Just verify that it landed on a
   * real entry before returning the cursor to the caller. */
  if (head->flags & QM_MULTIVALUE) {
    if (qmap_cursors[cur].pos >= head->sorted_n) {
      qmap_fin(cur);
      return QM_MISS;
    }
  } else if (qmap_cursors[cur].pos == QM_MISS) {
    qmap_fin(cur);
    return QM_MISS;
  }

  return cur;
}

  uint32_t /* API */
qmap_count(uint32_t hd, const void *key)
{
  qmap_head_t *head = &qmap_heads[hd];

  if (key == NULL) {
    /* Count total entries in map */
    return head->n;
  }

  /* For non-multivalue maps, return 0 or 1 */
  if (!(head->flags & QM_MULTIVALUE)) {
    return qmap_get(hd, key) != NULL ? 1 : 0;
  }

  /* For multivalue maps, use binary search to find first and last
   * occurrences. This keeps the count path logarithmic even when the
   * duplicate run is large. */
  int first = qmap_bsearch_ex(hd, key, NULL, QMAP_BSEARCH_FIRST);
  if (first == -1)
    return 0;

  int last = qmap_bsearch_ex(hd, key, NULL, QMAP_BSEARCH_LAST);
  return (uint32_t)(last - first + 1);
}

/* }}} */

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

#define TYPES_MASK 0xFF

#define DEBUG_LVL 1

#define DEBUG(lvl, ...) \
	if (DEBUG_LVL > lvl) WARN(__VA_ARGS__)

#define VAL_ADDR(qmap, n) \
	(void **)(((char *) qmap->table) \
			+ sizeof(void *) * n)

static_assert(QM_MISS == UINT32_MAX, "assume U32INT_MAX");

enum QM_MBR {
	QM_KEY,
	QM_VALUE,
};

enum qm_internal_flags {
	QM_SDIRTY = 1, // sorted list needs rebuild
};

typedef struct {
	uint32_t types[2], n, m, mask, flags,
		 phd, sorted_n, iflags, dbid;
} qmap_head_t;

typedef struct {
	idm_t idm;

	uint32_t *map;  	// id -> n
	const void **omap;	// n -> key
	void **table;		// n -> values

	ids_t linked;
	qmap_assoc_t *assoc;

	uint32_t *sorted_idx;
} qmap_t;

typedef struct {
	uint32_t hd, pos, sub_cur, ipos, flags;
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
 */
static inline uint32_t
qmap_id(uint32_t hd, const void * const key)
{
	qmap_head_t *head = &qmap_heads[hd];
	qmap_t *qmap = &qmaps[hd];
	qmap_type_t *type = &qmap_types[head->types[QM_KEY]];

	size_t key_len = type->measure
		? type->measure(key)
		: type->len;
	uint32_t id = type->hash(key, key_len)
		& head->mask;

	uint32_t n;
	const void *okey;
	uint32_t probe_count = 0;

	while (1) {
		n = qmap->map[id];
		if (n == QM_MISS)
			break; // Found empty slot

		okey = qmap_key(hd, n);

		if (okey == NULL) {
			id++;
			id &= head->mask;
			probe_count++;
			if (probe_count >= head->m)
				break;
			continue;
		}

		size_t len;
		if (type->measure) {
			size_t current_key_len = type->measure(key);
			size_t okey_len = type->measure(okey);
			len = (current_key_len > okey_len)
				? current_key_len
				: okey_len;
		} else
			len = type->len;

		if (type->cmp(okey, key, len) == 0)
			break;

		id++;
		id &= head->mask;
		probe_count++;
		if (probe_count >= head->m)
			break;
	}

	return id;
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
	qmap_type_t *type = &qmap_types[head->types[QM_KEY]];

	if (type->measure) {
		size_t len_a = type->measure(key_a);
		size_t len_b = type->measure(key_b);
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

static int
qmap_bsearch(uint32_t hd, const void *key, int *exact)
{
	qmap_head_t *head = &qmap_heads[hd];
	qmap_t *qmap = &qmaps[hd];

	if (head->iflags & QM_SDIRTY)
		qmap_rebuild_sorted(hd);

	qmap_type_t *type = &qmap_types[head->types[QM_KEY]];
	size_t key_len = qmap_len(head->types[QM_KEY], key);
	int low = 0, high = (int) head->sorted_n - 1;
	int mid = 0;

	*exact = 0;

	if (head->sorted_n == 0)
		return 0;

	while (low <= high) {
		mid = low + (high - low) / 2;
		const void *mid_key = qmap_key(hd, qmap->sorted_idx[mid]);

		size_t len;
		if (type->measure) {
			size_t mid_len = type->measure(mid_key);
			len = (key_len > mid_len) ? key_len : mid_len;
		} else
			len = type->len;

		int cmp = type->cmp(mid_key, key, len);

		if (cmp == 0) {
			*exact = 1;
			return mid;
		} else if (cmp < 0)
			low = mid + 1;
		else
			high = mid - 1;
	}

	return low;
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
	memset(qmap->table, 0, sizeof(void *) * len);
	// }}}

	if (flags & QM_SORTED) {
		qmap->sorted_idx = malloc(sizeof(uint32_t) * len);
		CBUG(!qmap->sorted_idx, "malloc error (sorted_idx)\n");
		head->iflags |= QM_SDIRTY;
	} else {
		qmap->sorted_idx = NULL;
		head->iflags &= ~QM_SDIRTY;
	}

	head->sorted_n = 0;

	memset(qmap->map, 0xFF, ids_len);
	memset(qmap->omap, 0, sizeof(void *) * len);

	return hd;
}

static inline void
qmap_load_file(char *filename, uint32_t dbid);

uint32_t /* API */
qmap_open(const char *filename,
		const char *database,
		uint32_t ktype, uint32_t vtype,
		uint32_t mask, uint32_t flags)
{
	uint32_t hd = _qmap_open(ktype, vtype, mask, flags);
	qmap_head_t *head = &qmap_heads[hd];

	if (database)
		head->dbid = XXH32(database, strlen(database), QM_SEED);
	else
		head->dbid = QM_MISS;

	if (!filename)
		goto file_skip;
	else {
		char buf[strlen(filename)
			+ strlen(database) + 2];

		snprintf(buf, sizeof(buf), "%s/%s",
				filename, database);

		const uint32_t *ehd = qmap_get(qmap_dbs_hd, buf);
		qmap_put(qmap_dbs_hd, buf, &hd);

		if (ehd && mdbs[*ehd])
			mdbs[*ehd] = 0;
		mdbs[hd] = 1;
	}

	const qmap_file_t *file_p
		= qmap_get(qmap_files_hd, filename);

	if (!file_p) {
		qmap_file_t file;
		memset(&file, 0, sizeof(file));
		file.ids = ids_init();
		ids_push(&file.ids, hd);
		qmap_put(qmap_files_hd, filename, &file);
	} else
		ids_push((ids_t *) &file_p->ids, hd);

file_skip:
	if (!(flags & QM_MIRROR))
		return hd;

	flags &= ~QM_AINDEX;
	_qmap_open(vtype, ktype, mask, flags | QM_PGET);
	qmap_assoc(hd + 1, hd, NULL);

	if (filename)
		qmap_load_file((char*) filename, head->dbid);
	return hd;
}

static size_t
s_measure(const void *key)
{
	return strlen(key) + 1;
}

static void file_close(qmap_file_t *file) {
	CBUG(munmap(file->mmaped, file->size) == -1,
			"munmap failed");

	close(file->fd);
	file->mmaped = 0;
}

__attribute__((destructor))
static void qmap_destruct(void) {
	qmap_save();

	for (uint32_t i = 0; i < idm.last; i++)
		qmap_close(i);

	idm_drop(&cursor_idm);
	idm_drop(&idm);

	uint32_t cur = qmap_iter(qmap_files_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, cur))
		file_close((qmap_file_t *) value);
}

__attribute__((constructor))
static void
qmap_init(void)
{
	qmap_type_t *type;
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
static inline uint32_t
_qmap_put(uint32_t hd, const void * key,
		const void *value, uint32_t pn)
{
	qmap_head_t *head = &qmap_heads[hd];
	qmap_t *qmap = &qmaps[hd];
	uint32_t n;
	uint32_t id;
	const void *aval = value;
	void *rval, *rkey;
	size_t klen;

	if (key) {
		uint32_t old_n;

		id = qmap_id(hd, key);
		old_n = qmap->map[id];

		if (old_n == QM_MISS) {
			n = idm_new(&qmap->idm);
			if (pn != QM_MISS)
				n = pn;
			head->n ++;
		} else
			n = old_n;
	} else {
		id = n = idm_new(&qmap->idm);
		head->n ++;
		key = &id;
	}

	CBUG(n >= head->m, "Capacity reached\n");
	DEBUG(2, "%u %u %u %p\n", hd, n, id, key);
	rkey = key ? (void *) key : &qmap->map[id];

	if (head->phd == hd) {
		if (head->types[QM_VALUE] == QM_PTR)
			value = &value;

		if (qmap->map[id] == n) {
			const void *ekey = qmap_key(hd, n);
			const void *eval = qmap_val(hd, n);

			free((void *) ekey);
			free((void *) eval);
		}

		klen = qmap_len(head->types[QM_VALUE], aval);
		rval = malloc(klen);
		* VAL_ADDR(qmap, n) = rval;
		memcpy(rval, value, klen);

		// this could be avoided
		// if the key is the same
		klen = qmap_len(head->types[QM_KEY], key);
		rkey = malloc(klen);
		memcpy(rkey, key, klen);
	}

	qmap->map[id] = n;
	qmap->omap[n] = rkey;

	if (head->phd == hd && (head->flags & QM_SORTED))
		head->iflags |= QM_SDIRTY;

	return id;
}

uint32_t /* API */
qmap_put(uint32_t hd, const void * const key,
		const void * const value)
{
	uint32_t ahd, n, id;
	idsi_t *cur;
	const void *rkey, *rval;

	id = _qmap_put(hd, key, value, QM_MISS);
	n = qmaps[hd].map[id];

	cur = ids_iter(&qmaps[hd].linked);
	rkey = qmap_key(hd, n);
	rval = qmap_val(hd, n);

	while (ids_next(&ahd, &cur)) {
		qmap_t *aqmap;
		const void *skey;

		aqmap = &qmaps[ahd];
		aqmap->assoc(&skey, rkey, rval);

		_qmap_put(ahd, skey, rval, n);
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

static void qmap_ndel_topdown(uint32_t hd, uint32_t n){
	qmap_head_t *head = &qmap_heads[hd];
	qmap_t *qmap = &qmaps[hd];
	const void *key, *value;
	uint32_t id, ahd;
	idsi_t *cur;

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

	if (head->phd == hd) {
		if (head->flags & QM_SORTED)
			head->iflags |= QM_SDIRTY;
		value = qmap_val(hd, n);
		free((void *) key);
		free((void *) value);
	}

	qmap->map[id] = QM_MISS;
	qmap->omap[n] = NULL;
	idm_del(&qmap->idm, n);
	head->n --;

}

/* Delete based on position */
static inline void
qmap_ndel(uint32_t hd, uint32_t n) {
	qmap_ndel_topdown(qmap_root(hd), n);
}

void /* API */
qmap_del(uint32_t hd, const void * const key)
{
	uint32_t cur = qmap_iter(hd, key, 0), sn;

	while (qmap_lnext(&sn, cur))
		qmap_ndel(hd, sn);
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

	if (key && (flags & QM_RANGE) && (head->flags & QM_SORTED)) {
		int exact;
		cursor->pos = qmap_bsearch(hd, key, &exact);
	} else if (key && !(flags & QM_RANGE)) {
		uint32_t id = qmap_id(hd, key);
		cursor->pos = qmap->map[id];
	} else
		cursor->pos = 0;

	cursor->ipos = cursor->pos;
	cursor->sub_cur = 0;
	cursor->hd = hd;
	cursor->key = key;
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

		*sn = qmap->sorted_idx[cursor->pos];
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
			size_t start_key_len = type->measure(cursor->key);
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
	uint32_t cur_id = qmap_iter(hd, NULL, 0), sn;

	while (qmap_lnext(&sn, cur_id))
		qmap_ndel(hd, sn);
}

void /* API */
qmap_close(uint32_t hd)
{
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
	free(qmap->map);
	free(qmap->omap);
	if (qmap->sorted_idx)
		free(qmap->sorted_idx);
	if (qmap_heads[hd].phd == hd)
		free(qmap->table);
	qmap->omap = NULL;
	idm_del(&idm, hd);
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
}

uint32_t /* API */
qmap_reg(size_t len)
{
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

	file->mmaped = (char*) mmap(NULL, file->size,
			PROT_READ, MAP_SHARED, file->fd, 0);

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
	size_t total_size = sizeof(size_t) + sizeof(head->n);
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

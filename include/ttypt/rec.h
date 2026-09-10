/**
 * @file rec.h
 * @brief Recall kernel: uniform candidate sets + a generic ranking loop.
 *
 * Pure C, no domain math. Candidate sets are sorted, deduplicated lists of
 * 64-bit refs (merge-join, no hashing). The ranking loop is a bounded,
 * streaming min-heap: an axis streams refs + scores straight into
 * rec_rank_push; a set is materialized only when a second axis needs to
 * join. Weighted composition across axes lives in the consumer's score
 * function.
 *
 * Kernel is optional and additive: raw qmap / domain entry points in the
 * axis libraries are untouched. Ref mapping is the consumer's job — the
 * kernel never interprets a ref.
 *
 * @see qmap.h
 */
#ifndef TTYPT_REC_H
#define TTYPT_REC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t rec_ref_t;

/** @defgroup rec_set Candidate sets
 *  @{
 */

typedef struct rec_set rec_set_t;

/** Create an empty, unsorted set (arena-backed, grows on demand). */
rec_set_t *rec_set_new(void);

/** Append a ref. Unseals a previously sealed set. */
void rec_set_push(rec_set_t *s, rec_ref_t r);

/** Sort + dedup in place. Joins require sealed inputs. */
void rec_set_seal(rec_set_t *s);

/** Drain a qmap handle's iteration into the set (keys must be fixed-length,
 *  <= 8 bytes; refs are read from the key bytes). Excludes nothing; call
 *  rec_set_seal afterwards. Returns 0 on success, -1 if keys are variable
 *  length or wider than a ref. */
int rec_set_fill_qmap_iter(rec_set_t *s, uint32_t hd);

/** dst := a ∩ b. a and b must be sealed; dst content is discarded. */
int rec_set_intersect(rec_set_t *dst, const rec_set_t *a, const rec_set_t *b);

/** dst := a − b. a and b must be sealed; dst content is discarded. */
int rec_set_subtract(rec_set_t *dst, const rec_set_t *a, const rec_set_t *b);

/** dst := a ∪ b. a and b must be sealed; dst content is discarded. */
int rec_set_union(rec_set_t *dst, const rec_set_t *a, const rec_set_t *b);

/** Number of refs currently held (works before and after sealing). */
size_t rec_set_count(const rec_set_t *s);

/** Get a pointer to the backing array (sealed → sorted, deduped). */
const rec_ref_t *rec_set_at(const rec_set_t *s);

/** Release the set's storage. */
void rec_set_free(rec_set_t *s);

/** @} */

/** @defgroup rec_rank Ranking loop
 *  @{
 */

typedef struct rec_rank rec_rank_t;

/** Score function over a ref. Returns 0 on success, nonzero on failure
 *  (ref skipped). score is written regardless on success. */
typedef int (*rec_score_fn)(void *ud, rec_ref_t r, float *score);

/** Create a ranking buffer bounded to top_k results (top_k == 0 → NULL).
 *  min_score drops refs below the threshold at push time. */
rec_rank_t *rec_rank_new(size_t top_k, float min_score);

/** Stream a scored candidate into the buffer. O(log top_k) when bounded. */
void rec_rank_push(rec_rank_t *rk, rec_ref_t r, float score);

/** Write the retained results sorted best-first (desc score; ties by asc
 *  ref) into caller arrays sized >= top_k. Returns the count written. */
size_t rec_rank_sorted(const rec_rank_t *rk, rec_ref_t *refs, float *scores);

/** Release the ranking buffer. */
void rec_rank_free(rec_rank_t *rk);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_REC_H */
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
 * Adapter contract: an axis exposes ONE filler
 *   int rec_axis_fill_*(<domain query args>, rec_set_t *out);
 * that streams the refs matching its filter into the set and seals it
 * (0 ok / -1 error). Fill-only queries stay streaming (the axis pushes
 * straight into rec_rank_push); a set is materialized only when a second
 * axis or ranker joins. Approximate fills (e.g. ANN neighbors) must
 * declare themselves, since intersecting with an approximate set bounds
 * final recall by it. Full design: docs/RECALL-KERNEL.md.
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

/** Exactness of a filled set. Approximate fills (e.g. ANN neighbors) must
 *  declare themselves: intersecting with an approximate set bounds final
 *  recall by the approximate one. Default for every set: exact, bound 1.0. */
enum rec_set_exactness {
	REC_SET_EXACT = 0, /**< every matching ref is reported */
	REC_SET_APPROX = 1 /**< bounded candidate window m; owed recall@k < 1 */
};

/** Mark the set approximate/exact and its owed recall@k bound (0 < bound
 *  <= 1). Exact sets carry bound 1.0. Returns 0 on success, -1 when the
 *  set is NULL or the bound is outside (0,1] (the flag is left unchanged).
 *  Joins propagate exactness: intersect/union produce an approximate set
 *  when either operand is (bound = min); subtract keeps the left operand's
 *  flag and bound (the result is a subset of it). */
int rec_set_set_approx(rec_set_t *s, int approx, float recall_bound);

/** 1 if the set is approximate (REC_SET_APPROX), 0 if exact (default). */
int rec_set_approx(const rec_set_t *s);

/** Owed recall@k bound (default 1.0 for exact sets). */
float rec_set_recall_bound(const rec_set_t *s);

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
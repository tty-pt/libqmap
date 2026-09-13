# The Recall Kernel — `rec.h`

A domain-free **"filter by axis → join → rank"** loop over uniform 32-bit
qmap refs. The kernel lives in this library (`include/ttypt/rec.h`,
`src/rec.c`). It knows nothing about time, space, text, or meaning: it
collects refs, combines candidate sets, and keeps the best-ranked results.
Axis libraries stay independent domain stores and feed the kernel through
small **adapters**; consumers compose axes without re-writing join/rank code.

The `rec_query` engine (below) turns that composition into a registry-driven
query object with `dlopen`-loaded axis plugins — so the kernel composes axes
without the consumer hand-writing the fill/join/rank loop.

- **Optional and additive.** Raw qmap / axis entry points are untouched; the
  kernel and engine are convenience layers, never mandatory wrappers.
- **Pure C, no domain math.** Weighted composition across axes lives in the
  consumer's score function.
- **Ref mapping is the consumer's job.** The kernel never interprets a ref.

## The pattern

Every search axis says one thing: *"these refs match my filter."* Searches
are:

1. **Filter** — each axis pours matching refs into a candidate set.
2. **Join** — combine axes (union, intersect, subtract) to keep refs that
   survive every applied axis.
3. **Rank** — score each surviving ref and keep the best `k`.

```c
rec_set_t  *cands = rec_set_new();
rec_rank_t *board = rec_rank_new(10, 0.4f);

rec_axis_fill_bbox(geo_db, s, l, 3, cands);        /* filter:  space    */
rec_axis_fill_interval(it_db, a, b, cands);        /* filter:  time     */
/* cands is now the intersection (in-box AND in-interval), sealed.        */

for (size_t i = 0; i < rec_set_count(cands); i++) {
    rec_ref_t ref = rec_set_at(cands)[i];
    float score;
    if (consumer_rank(ref, &score) == 0)          /* rank:    meaning  */
        rec_rank_push(board, ref, score);          /* top-10 kept        */
}
rec_rank_sorted(board, best_refs, best_scores);    /* best first */
```

## Building blocks

### Candidate sets — `rec_set_t`

An arena-backed list of `rec_ref_t` (uint32 — a qmap ref, never an axis's
own internal key). Append with `rec_set_push`,
then **seal** (sort + dedup) before any join; joins are sorted **merge-joins
— no hashing** and produce sealed output.

```c
rec_set_t *s = rec_set_new();
rec_set_push(s, ref);               /* append, unsorted while filling */
rec_set_seal(s);                    /* sort + dedup in place          */
rec_set_intersect(d, a, b);         /* d := a ∩ b  (a,b sealed)       */
rec_set_union(d, a, b);             /* d := a ∪ b  (a,b sealed)       */
rec_set_subtract(d, a, b);          /* d := a − b  (a,b sealed)       */
size_t          n = rec_set_count(s);
const rec_ref_t *refs = rec_set_at(s);   /* sealed → sorted, deduped  */
rec_set_free(s);
```

A push **unseals** the set — append more and seal again. Any qmap sorted
handle with fixed-length keys ≤ 4 bytes can be drained directly with
`rec_set_fill_qmap_iter(s, hd)`.

### Ranking buffers — `rec_rank_t`

A bounded **top-k min-heap** with a score threshold. Stream scored
candidates in; only the best `k` survive; `rec_rank_sorted` writes them
best-first (desc score, ties by asc ref).

```c
typedef int (*rec_score_fn)(void *ud, rec_ref_t r, float *score);

rec_rank_t *board = rec_rank_new(top_k, min_score);  /* top_k==0 → NULL */
rec_rank_push(board, ref, score);                    /* O(log top_k)    */
size_t n = rec_rank_sorted(board, refs, scores);
rec_rank_free(board);
```

## Kernel vs axes

| Owns | The kernel | An axis (libjoint, libislet, stoma, …) |
|---|---|---|
| Storage | — (no domain data) | its domain store (interval DB, morton index, inverted index) |
| Candidates | `rec_set_t` (fill/intersect/subtract/union/count) | its filter result, poured into a set via its adapter |
| Ranking | `rec_rank_t` loop, min-score, top-k, stable sort | its score function (recency, distance, FTS score, cosine) |
| Query composition | a `rec_set_t` + score fn = a query | nothing — axes only answer "which refs match" |

## The adapter contract

An axis exposes **one** function that streams the match results of a domain
query into a sealed set:

```c
int rec_axis_fill_*(<domain query args>, rec_set_t *out);
```

Rules (the contract):

1. **Signature** — returns plain `int` (0 ok / −1 error), consistent with
   `rec.h`'s `int`-return convention. No bespoke result enums.
2. **Streams, does not materialize.** The adapter pushes refs as it walks;
   it never builds an intermediate result array (e.g. a full box-volume).
   Sparse queries over huge domains stay cheap.
3. **Seals.** The adapter leaves `out` sealed (sorted, deduped) so the next
   adapter can merge-join against it. A ref reachable through several
   matches enters the set once.
4. **Additive.** Raw axis entry points remain; the adapter is optional.
5. **The ref is the axis's native id, widened.** libislet stores uint32 cell
   values and widens them to `rec_ref_t` at fill time; stoma fills the
   caller's decimal row ids (`rec_axis_fill_tokens` pushes `strtoull`
   of the row id, `stoma_rank` reverses it). The kernel never interprets a
   ref; mapping back to a consumer schema is the consumer's job.

## Streaming vs sets

Filter-only queries **never materialize a set**: a single axis can stream
straight into `rec_rank_push` via its own walker. A set is built only when a
second axis or a ranker needs a join. The adapter functions above always
write into a set because that is their contract; raw streaming is available
on the axis's own iterators.

## Exact vs approximate fills

Most fills are **exact**: every matching ref is reported, so intersecting
with anything keeps exact results. Approximate axes (e.g. ANN/HNSW semantic
neighbors) report a bounded candidate window `m` and cannot promise full
recall. An approximate fill must **declare itself** (a flag on the set plus
owed recall@k — implemented: `rec.h` `rec_set_set_approx()` /
`rec_set_approx()` / `rec_set_recall_bound()`; joins propagate: intersect/
union → approximate when either operand is, bound = min; subtract keeps the
left operand's flag), because intersecting with an approximate set bounds
final recall by the approximate one — `m` is the visible knob.

| Axis | Fill | Rank | Exact? |
|---|---|---|---|
| libjoint | interval membership `[a,b)` | optional recency | exact |
| libislet | bbox membership | optional distance | exact |
| stoma | lexical token set | FTS score | exact |
| libsepal (ANN) | **approximate** Hamming top-m | exact cosine | approximate (flag) |

## Query engine & plugin registry (`rec_query`)

The `rec_query` engine is the composition layer the kernel's pattern implies.
It is a registry-driven query running wholly inside libqmap with **zero axis
dependencies**: integer slots, opaque `void*` params and ctx, and a name
string used for introspection only. See `mm-plan/PHASE-2-CLI.md` (site repo)
for how this composes into the general qmap CLI surface.

### The axis object

```c
typedef int (*rec_fill_fn)(void *ctx, void *params, rec_set_t *out);
typedef int (*rec_rank_fn)(void *ctx, void *params, rec_ref_t ref, float *score);

typedef struct rec_axis {
    char        name[16];                         /* introspection only */
    rec_fill_fn fill;                             /* NULL = rank-only  */
    rec_rank_fn rank;                             /* NULL = filter-only*/
    void       *ctx;                              /* NULL until set    */
    void      *(*decode)(const char *s);          /* optional CLI parse */
} rec_axis_t;
```

### Two-phase registration

An axis `.so` registers itself from a constructor:

```c
__attribute__((constructor))
static void reg(void) {
    static const rec_axis_t a = { "time", time_fill, NULL, NULL, time_decode };
    rec_axis_register(&a);          /* → slot; ctx still NULL */
}
```

The consumer loads the `.so` with `dlopen` (the constructor runs and fills
the libqmap registry) and then binds a live store handle after opening it:

```c
int slot = rec_axis_register(...);        /* from the constructor, or looked up */
rec_axis_set_ctx(slot, store_handle);
```

#### The `rec_axis_open` convention (optional, CLI-specific — not core API)

An axis `.so` **may** additionally export a plain C symbol:

```c
void *rec_axis_open(const char *spec);
```

which opens whatever store the axis needs from an opaque spec string (a
path, a `path:opts` string, several `key=val` sub-specs joined by a
delimiter — entirely the axis's own business) and returns the ctx pointer
a consumer then passes to `rec_axis_set_ctx()`. This is **not** part of
the `rec_query` registry API — libqmap never declares, exports, or calls
`rec_axis_open` itself; it is purely a naming convention a `dlopen`-based
consumer (such as `qmap -Q`'s `--open SPEC` flag) can rely on to bind a
freshly-loaded axis without per-axis-specific glue code. Keeps the
"zero axis dependency" invariant intact: this is CLI↔axis-plugin
convention, not kernel-declared API.

### Running a query

```c
typedef enum { REC_JOIN_AND = 0, REC_JOIN_OR = 1, REC_JOIN_NOT = 2 } rec_join_t;

rec_query_t q = {0};
q.n_axes = 3;
q.axes[0] = (rec_query_axis_t){ it_slot,  &tp, REC_JOIN_AND };   /* seed  */
q.axes[1] = (rec_query_axis_t){ geo_slot, &gp, REC_JOIN_AND };   /* ∩ bbox */
q.axes[2] = (rec_query_axis_t){ st_slot,  &ap, REC_JOIN_AND };   /* ∩ FTS */
q.top_k = 10;
q.min_score = 0.4f;
q.consumer_score = my_score;            /* NULL → first rank-capable axis */

rec_query_run(&q, best_refs, best_scores);
```

The first axis seeds the running set; each subsequent axis's `join` selects
intersect (AND), union (OR), or subtract (NOT). A global `combine` field
applies one mode to every axis after the seed. Exactness propagates via the
kernel's joins (intersect → approx when either operand is, bound = min;
union → approx when either is; subtract keeps the left operand's flag).

The optional `consumer_score` callback receives a pre-computed
`rec_axis_score_t[]` (slot + score + valid per axis, regardless of join) for
each surviving ref, so the consumer weights axes however it likes. The
optional per-axis `decode` fn turns a CLI string into axis-owned params,
keeping the CLI generic.

### Plugin loading stays consumer-side

`dlopen` happens in the CLI or library user, never in libqmap's own link
table: `ldd bin/qmap` shows only `libqmap` + `libqsys` + libc + libxxhash,
and a `grep -E 'stoma|libjoint|libislet|libsepal'` across libqmap source
and headers returns zero matches.

## Status

- Kernel (`rec.h`/`rec.c` + `rec_test`, `bench_rec`): landed in this repo,
  branch `kernel`.
- libislet adapter `rec_axis_fill_bbox`: implemented (libislet branch
  `kernel`).
- libjoint adapter `rec_axis_fill_interval`: implemented (libjoint branch
  `kernel`, via the libjoint `rec_axis_t` registration).
- stoma adapter `rec_axis_fill_tokens` + ranker `stoma_rank`: implemented
  (site `external/stoma`).
- semantic (libsepal) adapter: implemented — `sepal_fill_approx` (streams
  top-m Hamming refs, seals, declares `REC_SET_APPROX` with owed bound
  `m/n`) + `sepal_rank` (exact cosine callback); sibling repo
  `/home/quirinpa/libsepal` v0.1.0. Search itself is network-free pure
  ANN; embedding stays consumer-side (no `rec_embed_t` in libsepal —
  the embedding ABI question in §4.2 stays open).
- `rec_query` engine (`rec_axis_register` / `rec_query_run` / `rec_join_t`):
  **done** — registry-driven query composition landed.
  Axis libs gain constructors that register; the CLI gains a `-Q`
  `dlopen`-plugin recall-query mode (`qmap -Q --dl PATH --open SPEC
  --axis N --params STR [--and|--or|--not] ... --combine MODE --top K
  --min F --list-axes`), plus a `QMAP_AXIS_LIBS` env var for auto-loading.
  A mock two-axis plugin (`external/libqmap/src/librec_axis_mock.c`) and
  `test-cli.sh` (wired into `make test`) smoke-test the full CLI path
  without needing any real axis's production data. `rec_axis_open` is
  now also implemented in all 4 real sibling libs (see the table below)
  and verified end-to-end against `qmap -Q` with a real, file-backed
  libsepal store (put two vectors, `--dl libsepal.so --open PATH --axis
  0 --params 'file=... qdim=... m=... min_sim=...'` recovered both refs
  with exact expected cosine scores 1.0/0.0, and `--min` correctly
  filtered the low-score one out). A `bin/qsearch` site wrapper script
  remains a deferred future workstream (no real site module writes
  through these axes yet).

| Axis | `rec_axis_open(spec)` convention | ctx type |
|---|---|---|
| libjoint | `spec` = `joint_init()` filename (empty/NULL → in-memory) | `jd` (unsigned handle, widened via `uintptr_t`) |
| libislet | `spec` = `"filename:database:mask"` (`:`-separated, any field empty → `islet_open()`'s NULL/0 default) | `uint32_t` db handle (widened via `uintptr_t`) |
| libsepal | `spec` = `sepal_open()` fname (empty/NULL → memory-only store) | `sepal_vecstore_t *` (direct pointer) |
| stoma | `spec` = decimal `stoma_open()` mask (empty/NULL → 0, qmap default) | `stoma_db_t *` (direct pointer) |

## Verification

- `src/rec_test.c` — sets, sorts, dedup, joins (intersect/subtract/union),
  `rec_set_fill_qmap_iter`, rank min-score/top_k/ties/streaming; 18 groups.
  Plus the 1C exactness group: fresh-set defaults (exact/bound 1.0),
  set/clear, bound validation (`(0,1]`, NULL → −1), join-propagation matrix
  (intersect/union → approx + bound=min; subtract keeps left's).
- `src/bench_rec.c` — kernel join vs hand-rolled join parity (identical
  match counts); equal/zero `qmap_next` scans on the kernel path.
- libislet: `test_geo_fill.c`, `test_fill_parity.c` (fill == raw collect),
  `test_fill_props.c` (brute-force oracle), `bench_rec_fill.c`,
  `bench_sparse.c`.
- stoma: `stoma_test.c` groups 30-35 (fill==query equivalence both phrase
  modes, seal/additivity, arg validation + non-decimal strictness, rank
  numbers, kernel join/rank integration), `stoma_prop_test.c` (fixed-LCG
  differential: fill set == reference == stoma_query for every query, plus
  exact rank-score spot-checks).
- libsepal: 8 unit tiers (blob/cosine/sketch/matryoshka/vecstore/search/
  fill/rank — search/fill validated against an exact float-cosine
  brute-force reference), `test_persistence` (close→reopen byte-identical),
  `test_recall_prop` (seeds {1,42,1337} × dims {64,256,384,768} × N to
  3000: search==brute-force order with full pool, fill ⊇ top-k),
  `test_blob_props` (20k random buffers never misread), `test_scan_stress`
  (10k×768), standalone fuzzers; `bench_search` (two-stage vs brute force,
  see study §12.4).
/* librec_axis_mock.c — mock rec_axis plugin for the -Q CLI smoke test
 * (PLAN-REC-QUERY.md §4.4, Part 3.4). Registers TWO axes in one
 * constructor — "mock_a" (fill+rank+decode) and "mock_b" (fill-only, no
 * rank, no decode) — so a single `--dl` brings in both halves of an
 * AND/OR/NOT composition test.
 *
 * (Deliberately one plugin with two axes, not two plugins: `dlopen` caches
 * by path, so two independently-`dlopen`'d *different* .so files would
 * each need their own Makefile "lib" target — but ../mk/include.mk's
 * LIB-obj-y aggregation has a latent bug with 3+ concurrent "lib*" targets
 * in one Makefile ($(LIB:%=%-obj-y) expands to a multi-word string, and
 * GNU Make's $($(X)) double-dereference only works for a single-word X,
 * so libqmap's own extra objects — idm.o/rec.o/rec_axis.o — silently drop
 * out of the link line). Rather than touch that shared, cross-repo build
 * system, this test stays a single "lib" target and gets its two
 * independent axes from one constructor instead.)
 *
 * Built as a loadable .so (Makefile's `all` list, name starts with "lib" so
 * the shared LIB build rule picks it up) but NEVER linked into qmap/libqmap
 * themselves — dlopen'd only by test-cli.sh via `qmap -Q --dl ...`. Mirrors
 * the shape a real axis library's registration constructor would have
 * (see PLAN-REC-QUERY.md §3's four sibling axis libraries) except for the
 * two-axes-in-one-constructor deviation explained above, which is purely a
 * test-plugin convenience, not a convention real axis libs should follow.
 */
#include <ttypt/rec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	rec_ref_t *refs;
	size_t n;
} mock_ctx_t;

static int mock_a_slot = -1;
static int mock_b_slot = -1;

static int
mock_a_fill(void *ctx, void *params, rec_set_t *out)
{
	(void) params;
	mock_ctx_t *c = ctx;
	if (!c)
		return -1;
	for (size_t i = 0; i < c->n; i++)
		rec_set_push(out, c->refs[i]);
	rec_set_seal(out);
	return 0;
}

static int
mock_a_rank(void *ctx, void *params, rec_ref_t ref, float *score)
{
	(void) ctx;
	(void) params;
	*score = (float) ref;
	return 0;
}

/* No query-time params needed by this mock; pass the raw string straight
 * through so the CLI's --params plumbing has something non-NULL to forward
 * (exercises the "decode present, params forwarded" path in qmap.c). */
static void *
mock_a_decode(const char *s)
{
	return (void *) s;
}

static int
mock_b_fill(void *ctx, void *params, rec_set_t *out)
{
	(void) params;
	mock_ctx_t *c = ctx;
	if (!c)
		return -1;
	for (size_t i = 0; i < c->n; i++)
		rec_set_push(out, c->refs[i]);
	rec_set_seal(out);
	return 0;
}

__attribute__((constructor))
static void
mock_init(void)
{
	static const rec_axis_t a = {
		"mock_a", mock_a_fill, mock_a_rank, NULL, mock_a_decode
	};
	static const rec_axis_t b = { "mock_b", mock_b_fill, NULL, NULL, NULL };
	mock_a_slot = rec_axis_register(&a);
	mock_b_slot = rec_axis_register(&b);
}

/* Parses one ref list into a malloc'd mock_ctx_t. "refs=" prefix optional. */
static mock_ctx_t *
parse_refs(const char *s)
{
	if (!s)
		return NULL;

	const char *eq = strchr(s, '=');
	const char *list = eq ? eq + 1 : s;

	size_t cap = 8, n = 0;
	rec_ref_t *refs = malloc(cap * sizeof(*refs));
	if (!refs)
		return NULL;

	char *copy = strdup(list);
	if (!copy) {
		free(refs);
		return NULL;
	}

	char *save = NULL;
	for (char *tok = strtok_r(copy, ",", &save); tok;
			tok = strtok_r(NULL, ",", &save)) {
		if (n == cap) {
			cap *= 2;
			rec_ref_t *grown = realloc(refs, cap * sizeof(*refs));
			if (!grown) {
				free(refs);
				free(copy);
				return NULL;
			}
			refs = grown;
		}
		refs[n++] = (rec_ref_t) strtoull(tok, NULL, 10);
	}
	free(copy);

	mock_ctx_t *ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		free(refs);
		return NULL;
	}
	ctx->refs = refs;
	ctx->n = n;
	return ctx;
}

/* rec_axis_open convention (PLAN-REC-QUERY.md §4.3 / D14): spec is
 * "a=1,2,3:b=2,3,4" — one ref list per axis this plugin owns, separated by
 * ':'. Binds mock_a's ctx directly via rec_axis_set_ctx() (this plugin
 * knows its own slot from mock_init()'s rec_axis_register() return value)
 * and returns mock_b's ctx for the CLI's own rec_axis_set_ctx(new_slot,
 * ...) call to bind (new_slot is always the *last* slot this dlopen
 * registered — mock_b, since it was registered second). Never freed
 * (process lifetime — dlclose is never called on axis plugins, matching
 * real store handles). */
void *
rec_axis_open(const char *spec)
{
	if (!spec)
		return NULL;

	char *copy = strdup(spec);
	if (!copy)
		return NULL;

	char *save = NULL;
	char *part_a = strtok_r(copy, ":", &save);
	char *part_b = strtok_r(NULL, ":", &save);

	mock_ctx_t *ctx_a = parse_refs(part_a);
	mock_ctx_t *ctx_b = parse_refs(part_b);
	free(copy);

	if (mock_a_slot >= 0 && ctx_a)
		rec_axis_set_ctx(mock_a_slot, ctx_a);

	return ctx_b;
}

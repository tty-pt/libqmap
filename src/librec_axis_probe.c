/* librec_axis_probe.c — single-axis probe plugin for the L1 short-circuit
 * efficiency gate (test-shortcircuit.sh): proves qmap_expr_eval skips the
 * decode/fill of AND/EXCEPT branches once the running set is empty
 * (AXIS-EFF plan L1).
 *
 * Axis "pe":
 *   - decode(value): returns a heap copy (process-lifetime params, matches
 *     the fold/plain decode convention) and, for the special probe value
 *     "boom", prints a stderr marker "AXIS-BOOM-DECODED\n" — the
 *     black-box observation point the shell test greps for.
 *   - fill(ctx, params, out): pushes the ctx's fixed ref list {1,2,3},
 *     EXCEPT when params == "empty", which returns the empty set.
 *
 * rec_axis_open ignores the alongside spec (this axis stores nothing on
 * disk) and returns a fresh ctx holding refs {1,2,3}. Test-only.
 */
#include <ttypt/rec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	rec_ref_t refs[3];
	size_t     n;
} probe_ctx_t;

static int
probe_fill(void *ctx, void *params, rec_set_t *out)
{
	probe_ctx_t *c = ctx;

	if (!c || !out)
		return -1;
	if (params && !strcmp((const char *)params, "empty")) {
		rec_set_seal(out);
		return 0;
	}
	for (size_t i = 0; i < c->n; i++)
		rec_set_push(out, c->refs[i]);
	rec_set_seal(out);
	return 0;
}

static void *
probe_decode(const char *s)
{
	char *copy;

	if (!s)
		return NULL;
	if (!strcmp(s, "boom"))
		fprintf(stderr, "AXIS-BOOM-DECODED\n");
	copy = strdup(s);
	return copy;
}

__attribute__((constructor))
static void
probe_init(void)
{
	static const rec_axis_t pe = {
		"pe", probe_fill, NULL, NULL, probe_decode
	};

	rec_axis_register(&pe);
}

void *
rec_axis_open(const char *spec)
{
	static const rec_ref_t refs[] = { 1, 2, 3 };
	probe_ctx_t *c = calloc(1, sizeof(*c));

	(void) spec;
	if (!c)
		return NULL;
	memcpy(c->refs, refs, sizeof(refs));
	c->n = 3;
	return c;
}
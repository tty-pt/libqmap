/**
 * @file rec_cli.c
 * @brief Kernel-owned axis CLI ABI: decode-spec scanner + strict value
 *        parsers (rec.h @ref rec_axis_cli).
 *
 * These are the SINGLE implementation of the D14/D15B shared helpers. An
 * axis library defines only its option table (rec_axis_cli_options) and
 * its per-field mapping; everything else — the struct layout, the
 * decode-spec grammar, the strict value parsing, the owned-string setter —
 * lives here in the kernel and is shared by every TU that includes
 * <ttypt/rec.h>. Never re-declare any of this locally.
 */
#include <ttypt/rec.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
rec_cli_str_set(char **dst, const char *value)
{
	char *copy;

	if (!dst || !value)
		return -1;
	copy = strdup(value);
	if (!copy)
		return -1;
	free(*dst);
	*dst = copy;
	return 0;
}

int
rec_spec_next(char **cur, char **key, char **val)
{
	char *p, *s, *w;

	if (!cur || !*cur)
		return 0;
	p = *cur;
	for (;;) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p) {
			*cur = p;
			return 0;
		}
		s = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '=')
			p++;
		if (key)
			*key = s;
		if (*p == '=')
			break;
		/* bare token (no value): skip it and keep scanning. A plain
		 * loop, not recursion — a spec of many bare tokens must not
		 * cost a stack frame per token. */
		if (*p)
			p++;
		if (val)
			*val = NULL;
	}
	*p = '\0';
	p++;
	if (*p == '\'') {
		p++;
		s = p;                 /* value start */
		w = p;                 /* in-place unescape cursor */
		while (*p && *p != '\'') {
			if (*p == '\\' && p[1])
				p++;
			*w++ = *p++;
		}
		if (*p == '\'')
			p++;
		*w = '\0';
	} else {
		s = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
	}
	if (val)
		*val = s;
	*cur = p;
	return 1;
}

int
rec_cli_int(const char *v, int *out)
{
	char *end = NULL;
	long n;

	if (!v || !out)
		return -1;
	errno = 0;
	n = strtol(v, &end, 10);
	if (errno == ERANGE || end == v || *end != '\0' ||
	    n < INT_MIN || n > INT_MAX)
		return -1;
	*out = (int)n;
	return 0;
}

int
rec_cli_uint(const char *v, unsigned *out)
{
	char *end = NULL;
	unsigned long n;

	if (!v || !out)
		return -1;
	if (v[0] == '-')
		return -1;
	errno = 0;
	n = strtoul(v, &end, 10);
	if (errno == ERANGE || end == v || *end != '\0' || n > UINT_MAX)
		return -1;
	*out = (unsigned)n;
	return 0;
}

	int
rec_cli_size(const char *v, size_t *out)
{
	char *end = NULL;
	unsigned long long n;

	if (!v || !out)
		return -1;
	if (v[0] == '-')
		return -1;
	errno = 0;
	n = strtoull(v, &end, 10);
	if (errno == ERANGE || end == v || *end != '\0' || n > SIZE_MAX)
		return -1;
	*out = (size_t)n;
	return 0;
}

int
rec_cli_float(const char *v, float *out)
{
	char *end = NULL;
	float f;

	if (!v || !out)
		return -1;
	errno = 0;
	f = strtof(v, &end);
	if (end == v || *end != '\0')
		return -1;
	/* magnitude overflow rejects; underflow → ±0 is accepted */
	if (errno == ERANGE && f != 0.0f)
		return -1;
	*out = f;
	return 0;
}
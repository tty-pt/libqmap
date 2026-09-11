/**
 * @page qmap qmap(1)
 * @brief Qmap command-line tool — create, query, and associate persistent maps.
 *
 * Simple CLI for the Qmap engine. Replaces the old **qdb** and links directly against libqmap.
 *
 * ## Overview
 * Creates/opens key-value databases in memory or on disk; supports queries, associations, mirrors, and iteration.
 * Supports *uint32_t* and *string* types. **Auto-index is only active if the key type is `a`.**
 *
 * ## Installation
 * See: <https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages>
 *
 * ## Usage
 * ```
 * qmap [-qa ARG] [[-rl] [-RpdgmcD ARG] ...] file[[:k]:v]
 * ```
 * Run `qmap -?` to show full help.
 *
 * ### Options
 * - **-r**
 *   Reverse direction (swap key/value lookup).
 * - **-l**
 *   List all key/value pairs.
 * - **-L**
 *   List "missing" values (**requires `-q`** to specify the paired database).
 * - **-q** *file[:k[:v]]*
 *   Query database for lookups and printing.
 * - **-a** *file[:k[:v]]*
 *   Associate database for reverse lookups and printing.
 * - **-R** *KEY*
 *   Random value for *KEY* (`.` = any).
 * - **-p** *KEY[:VAL]*
 *   Insert or update a key/value pair.
 * - **-d** *KEY[:VAL]*
 *   Delete key/value pair (first match for multivalue maps).
 * - **-D** *KEY[:VAL]*
 *   Delete ALL entries with key (multivalue support).
 * - **-g** *KEY*
 *   Get value(s) for a key (first match for multivalue maps; `.` = all).
 * - **-m** *KEY*
 *   Get ALL values for a key (multivalue iteration).
 * - **-c** *KEY*
 *   Count entries for a key.
 * - **-x**
 *   When printing associations, stop after the first result.
 * - **-k**
 *   Also print keys (for `-g`, `-m`, and `-R`).
 *
 * ### Type specifiers
 * - **u** — uint32_t integer
 * - **s** — string (default for both key and value)
 * - **a** — key only: uint32_t with auto-index
 * - **2&lt;type&gt;** — key only: multivalue support (enables QM_MULTIVALUE|QM_SORTED)
 *
 * ### Options
 * - **-r**
 *   Reverse direction (swap key/value lookup).
 * - **-l**
 *   List all key/value pairs.
 * - **-L**
 *   List “missing” values (**requires `-q`** to specify the paired database).
 * - **-q** *file[:k[:v]]*
 *   Query database for lookups and printing.
 * - **-a** *file[:k[:v]]*
 *   Associate database for reverse lookups and printing.
 * - **-R** *KEY*
 *   Random value for *KEY* (`.` = any).
 * - **-p** *KEY[:VAL]*
 *   Insert or update a key/value pair.
 * - **-d** *KEY[:VAL]*
 *   Delete key/value pair(s).
 * - **-g** *KEY*
 *   Get value(s) for a key (`.` = all).
 * - **-x**
 *   When printing associations, stop after the first result.
 * - **-k**
 *   Also print keys (for `-g` and `-R`).
 *
 * ### Notes
 * - `-r` is counter-intuitive: when enabled, lookups are done **by primary keys**.
 * - `-q`/`-a` options are processed in order; each entry extends the lookup chain.
 * - Multivalue maps (type `2<type>`) automatically enable QM_MULTIVALUE|QM_SORTED flags.
 *
 * ### Examples
 * @code
 * # Automatic IDs: key 'a', values as strings
 * qmap -p Mathew owners.db:a:s              # → Mathew's ID
 * qmap -p cat    pets.db:a:s                # → cat's ID
 *
 * # Association (no duplicates)
 * qmap -p 1:1 assoc.db:u:u                  # owner_id:pet_id
 *
 * # Get Mathew's pet (use -q/-a to resolve names/IDs)
 * qmap -q owners.db:a:s -a pets.db:a:s -g Mathew assoc.db:u:u
 *
 * # Random value for a KEY
 * qmap -q owners.db:a:s -a pets.db:a:s -R Mathew assoc.db:u:u
 *
 * # Chained lookups (multiple -q/-a in order)
 * qmap -q owners.db:a:s -q pets.db:a:s -g Mathew assoc.db:u:u
 *
 * # Multivalue support - duplicate keys
 * qmap -p 100:value1 multi.db:2u:s          # Create multivalue map
 * qmap -p 100:value2 multi.db:2u:s          # Add duplicate key
 * qmap -p 100:value3 multi.db:2u:s          # Add another
 * qmap -r -g 100 multi.db:2u:s              # → value1 (first match)
 * qmap -r -m 100 multi.db:2u:s              # → value1 value2 value3 (all)
 * qmap -r -c 100 multi.db:2u:s              # → 3 (count)
 * qmap -r -d 100 multi.db:2u:s              # Delete first only
 * qmap -r -D 100 multi.db:2u:s              # Delete all with key 100
 * @endcode
 *
 * ### Notes
 * - `-r` is counter-intuitive: when enabled, lookups are done **by primary keys**.
 * - `-q`/`-a` options are processed in order; each entry extends the lookup chain.
 * - Multivalue maps (type `2<type>`) automatically enable QM_MULTIVALUE|QM_SORTED flags.
 * - For multivalue maps with non-string keys, use `-r` to lookup by the key type.
 *
 * # Association (no duplicates)
 * qmap -p 1:1 assoc.db:u:u                  # owner_id:pet_id
 *
 * # Get Mathew’s pet (use -q/-a to resolve names/IDs)
 * qmap -q owners.db:a:s -a pets.db:a:s -g Mathew assoc.db:u:u
 *
 * # Random value for a KEY
 * qmap -q owners.db:a:s -a pets.db:a:s -R Mathew assoc.db:u:u
 *
 * # Chained lookups (multiple -q/-a in order)
 * qmap -q owners.db:a:s -q pets.db:a:s -g Mathew assoc.db:u:u
 * @endcode
 *
 * ### Notes
 * - `-r` is counter-intuitive: when enabled, lookups are done **by primary keys**.
 * - `-q`/`-a` options are processed in order; each entry extends the lookup chain.
 *
 * @see qmap_handle
 * @see qmap_common
 * @see qmap_assoc
 * @see qmap_iteration
 * @see qmap_type
 */

#include <ttypt/qmap.h>
#include <ttypt/idm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <ttypt/qsys.h>
#include <ttypt/rec.h>

#define DEBUG_LVL 0
#define DEBUG(lvl, ...) \
	if (DEBUG_LVL > lvl) WARN(__VA_ARGS__)

#define QM_MAX 1024
#define QDBE_MASK (32768 - 1)
#define QDBE_QMASK 0xF
/* CLI-specific flags - use high bits to avoid overlap with QM library flags (0x1F) */
#define QH_RDONLY 0x100
/* Mask for QMap library flags (bits 0-4: values 1,2,4,8,16) */
#define QM_FLAGS_MASK 0x1F

typedef struct {
	uint32_t types[2];
} qmape_meta_t;

typedef void qmape_print_t(const void *data);

typedef struct {
	qmape_print_t *print;
} qmape_type_t;

enum qmape_mbr {
	KEY,
	VALUE,
};

qmape_meta_t metas[QM_MAX];
qmape_type_t types[8];

uint32_t QH_NOT_NEW = 0x200;  /* CLI flag - high bit to avoid QM library flag overlap */

uint32_t prim_hd, aux_hd;

const void *value_ptr, *key_ptr;
char *col;

typedef struct {
	uint32_t hd, n;
} aq_t;

enum aq {
	AQ_A,
	AQ_Q,
};

aq_t aqs[2];

uint32_t reverse = 0, bail = 0, print_keys = 0;

uint32_t qmap_get_type;
const void **qmap_get_ptr;

void qmape_print(uint32_t hd, enum qmape_mbr t,
		const void *buf)
{
	qmape_meta_t *meta = &metas[hd];
	qmape_type_t *type = &types[meta->types[t]];
	type->print(buf);
}

void
usage(char *prog)
{
	fprintf(stderr, "Usage: %s [-qa ARG] [[-rl] [-RpdgmcD ARG] ...] file[[:k]:v]\n", prog);
	fprintf(stderr, "    Options:\n");
	fprintf(stderr, "        -r               reverse operation\n");
	fprintf(stderr, "        -l               list all values\n");
	fprintf(stderr, "        -L               list missing values\n");
	fprintf(stderr, "        -q file[:k[:v]]  db to use string lookups and printing\n");
	fprintf(stderr, "        -a file[:k[:v]]  db to use for reversed string lookups and printing\n");
	fprintf(stderr, "        -R KEY           get random value of key (empty key for any)\n");
	fprintf(stderr, "        -p KEY[:VAL]     put a key/value pair\n");
	fprintf(stderr, "        -d KEY[:VAL]     delete key/value pair (first match for multivalue)\n");
	fprintf(stderr, "        -D KEY[:VAL]     delete ALL entries with key (multivalue support)\n");
	fprintf(stderr, "        -g KEY           get value(s) of a key (first match for multivalue)\n");
	fprintf(stderr, "        -m KEY           get ALL values for a key (multivalue iteration)\n");
	fprintf(stderr, "        -c KEY           count entries for a key\n");
	fprintf(stderr, "        -x               when printing associations, bail on first result\n");
	fprintf(stderr, "        -k               also print keys (for get and rand).\n");
	fprintf(stderr, "    'k' and 'v' are key and value types. Supported values:\n");
	fprintf(stderr, "         u               uint32_t\n");
	fprintf(stderr, "         s               string (default for both key and value)\n");
	fprintf(stderr, "         a               key only! uint32_t automatic index\n");
	fprintf(stderr, "         2<base-type>    key only! multivalue support (enables QM_MULTIVALUE|QM_SORTED)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Use '.' as the KEY for all keys!\n");
	fprintf(stderr, "-q/-a options are processed in order.\n");
}

static inline uint32_t
qmape_type(uint32_t phd, enum qmape_mbr t, uint32_t reverse)
{
	qmape_meta_t *meta = &metas[phd];
	return meta->types[reverse ? !t : t];
}

static inline const void *rec_query(
		enum aq aq,
		const void *tbuf,
		const void *buf,
		uint32_t tmprev)
{
	tmprev = (aqs[AQ_Q].n & 1) == tmprev;
	uint32_t c2 = qmap_iter(aqs[aq].hd, NULL, 0);
	ids_t rqs = ids_init();
	uint32_t aux_hd;
	const void *key, *value, *aux2;

	uint32_t lktype = qmape_type(prim_hd,
			KEY, !reverse);

	while (qmap_next(&key, &value, c2)) {
		aux_hd = * (uint32_t *) value;

		if (qmape_type(aux_hd, VALUE, tmprev)
				!= lktype)
			tmprev = !tmprev;

		if (qmape_type(aux_hd, VALUE, tmprev)
			       != lktype)
		{
			// TODO free idml
			ids_drop(&rqs);
			fprintf(stderr, "Invalid query sequence\n");
			qmap_fin(c2);
			return NULL;
		}

		lktype = qmape_type(aux_hd, KEY, tmprev);
		ids_push(&rqs, aux_hd + tmprev);
	}

	while ((aux_hd = ids_pop(&rqs)) != (uint32_t) -1) {
		value = qmap_get(aux_hd, buf);
		if (!value) {
			ids_drop(&rqs);
			return NULL;
		}
		tbuf = value;
		aux2 = buf;
		buf = tbuf;
		tbuf = aux2;
	};

	ids_drop(&rqs);
	return buf;
}

static inline int gen_cond(int is_value) {
	uint32_t aq_hd = aqs[AQ_Q].hd;
	uint32_t c = qmap_iter(aq_hd, NULL, 0);
	uint32_t rev = !reverse;
	uint32_t type = qmape_type(prim_hd,
			is_value ? KEY : VALUE,
			rev);
	const void *key, *value;

	while (qmap_next(&key, &value, c)) {
		rev = !rev;
		uint32_t aux_hd = * (uint32_t *)
			value;
		type = qmape_type(aux_hd, KEY, rev);
	}

	return type == QM_STR;
}

inline static const void *
_gen_lookup(const void **buf, uint32_t *uret, char *str,
		enum aq aq, int is_value)
{
	uint32_t cond = gen_cond(is_value);
	const void *ret = NULL;

	if (cond)
		*buf = str;
	else {
		*uret = strtoul(str, NULL, 10);
		*buf = uret;
		/* fprintf(stderr, "_gen_lookup 2 %u\n", ret); */
	}

	if (aqs[aq].n)
		ret = rec_query(aq, *buf, *buf, !reverse);
	else
		ret = *buf;

	return ret;
}

static const void *gen_lookup(char *str) {
	static uint32_t aret, bret;

	if (!str || !strcmp(str, "."))
		return NULL;

	col = strchr(str, ':');
	if (col) {
		*col = '\0';
		col++;
		_gen_lookup(&value_ptr, &aret, col, AQ_A, 1);
		key_ptr = str;
		return _gen_lookup(&key_ptr, &bret, str,
				AQ_Q, 0);
	}

	return _gen_lookup(&value_ptr, &bret, str,
			AQ_Q, 1);
}

static inline void gen_del(void) {
	gen_lookup(optarg);
	qmap_del(prim_hd + !reverse, value_ptr);
}

static inline int assoc_exists(const char *key_ptr) {
	if (!aqs[AQ_A].n)
		return 1;

	return !!rec_query(AQ_A, key_ptr,
			key_ptr, 1);
}

static inline void assoc_print(void) {
	const void *alt_ptr;
	const void *buf = reverse ? value_ptr : key_ptr;
	uint32_t aux_hd;
	uint32_t aq_hd = aqs[AQ_A].hd;
	uint32_t c2 = qmap_iter(aq_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c2)) {
		aux_hd = * (uint32_t *)
			value;
		putchar(' ');

		alt_ptr = qmap_get(aux_hd, buf);

		if (!alt_ptr) {
			printf("-1");
			continue;
		}

		qmape_print(aux_hd, VALUE, alt_ptr);

		if (bail)
			break;
	}
}

static inline void _gen_get(void) {
	if (print_keys) {
		qmape_print(prim_hd, KEY, key_ptr);
		putchar(' ');
		qmape_print(prim_hd, VALUE,
				value_ptr);
	} else {
		qmape_print(prim_hd, qmap_get_type,
				*qmap_get_ptr);
	}
	assoc_print();
	printf("\n");
}

static inline void gen_rand(void) {
	uint32_t count = 0, randn;
	uint32_t c;
	const void *iter_key = gen_lookup(optarg);

	// why iter by key, if we don't support dupes?
	// should we just get? spoilers: we use the key - or not.

	c = qmap_iter(prim_hd + !reverse, iter_key, 0);

	while (qmap_next(&key_ptr, &value_ptr, c))
		if (assoc_exists(key_ptr))
			count ++;

	if (count == 0) {
		printf("-1\n");
		return;
	}

	randn = rand() % count;
	c = qmap_iter(prim_hd + !reverse, iter_key, 0);

	while (qmap_next(&key_ptr, &value_ptr, c))
		if (!assoc_exists(key_ptr))
			continue;
		else if ((--count) <= randn) {
			qmap_fin(c);
			break;
		}

	_gen_get();
}

static void gen_get(char *str) {
	const void *iter_key = gen_lookup(str);
	uint32_t c;
	uint32_t nonce = 1, hd;
	const void *key;

	if (reverse) {
		qmap_get_ptr = &value_ptr;
		qmap_get_type = VALUE;
	} else {
		qmap_get_ptr = &key_ptr;
		qmap_get_type = KEY;
	}

	if (str && strcmp(str, ".") && !iter_key) {
		printf("-1\n");
		return;
	}

	hd = prim_hd + !reverse;
	c = qmap_iter(hd, iter_key, 0);

	while (qmap_next(&key, &value_ptr, c)) {
		if (reverse)
			key_ptr = key;
		else {
			key_ptr = value_ptr;
			value_ptr = key;
		}

		if (assoc_exists(key_ptr)) {
			_gen_get();
			nonce = 0;
		}
	}

	if (nonce)
		printf("-1\n");
}

static void gen_list(void) {
	uint32_t c;
	uint32_t cond, aux;

	gen_lookup(NULL);
	cond = gen_cond(1);

	c = qmap_iter(prim_hd, NULL, 0);

	qmap_get_type = VALUE;
	qmap_get_ptr = &key_ptr;
	aux = print_keys;
	print_keys = 1;

	while (qmap_next(&key_ptr, &value_ptr, c)) {
		rec_query(AQ_Q, key_ptr, value_ptr, !cond);
		_gen_get();
	}

	print_keys = aux;
}

static inline void gen_multi(void) {
	const void *iter_key = gen_lookup(optarg);
	uint32_t c;

	if (!iter_key) {
		printf("-1\n");
		return;
	}

	c = qmap_get_multi(prim_hd + !reverse, iter_key);
	
	if (c == QM_MISS) {
		printf("-1\n");
		return;
	}

	while (qmap_next(&key_ptr, &value_ptr, c)) {
		if (reverse)
			key_ptr = value_ptr;
		
		if (print_keys) {
			qmape_print(prim_hd, KEY, key_ptr);
			putchar(' ');
		}
		qmape_print(prim_hd, VALUE, value_ptr);
		putchar('\n');
	}
}

static inline void gen_count(void) {
	const void *iter_key = gen_lookup(optarg);
	size_t count;

	if (!iter_key) {
		printf("-1\n");
		return;
	}

	count = qmap_count(prim_hd + !reverse, iter_key);
	printf("%zu\n", count);
}

static inline void gen_del_all(void) {
	gen_lookup(optarg);
	qmap_del_all(prim_hd + !reverse, value_ptr);
}

static inline void gen_put(void) {
	uint32_t id;
	const void *key;

	gen_lookup(optarg);

	key = col ? key_ptr : NULL;
	id = qmap_put(prim_hd, key, value_ptr);
	qmape_print(prim_hd, KEY,
			key ? key : (char *) &id);
	putchar('\n');
}

static inline void gen_list_missing(void) {
	uint32_t c;
	gen_lookup(NULL);

	if (!aqs[AQ_Q].n) {
		fprintf(stderr, "list missing needs a corresponding database\n");
		return;
	}

	c = qmap_iter(prim_hd + !reverse, NULL, 0);
	while (qmap_next(&key_ptr, &value_ptr, c)) {
		uint32_t aqs_hd = aqs[AQ_Q].hd;
		uint32_t c2 = qmap_iter(aqs_hd, NULL, 0);
		const void *skey, *sval;

		while (qmap_next(&skey, &sval, c2)) {
			uint32_t ahd = (* (uint32_t *)
				sval)
				+ !reverse;
			key_ptr = qmap_get(ahd, value_ptr);
			if (key_ptr)
				_gen_get();
		}
	}
}

uint32_t _qmape_type(char *which) {
	if (*which == '2' && which[1] != '\0')
		which++;
	if (*which == 's')
		return QM_STR;
	if (*which == 'u')
		return QM_U32;
	fprintf(stderr, "Invalid type specifier: %s\n", which);
	exit(EXIT_FAILURE);
}

uint32_t gen_open(char *fname, uint32_t flags) {
	char buf[BUFSIZ];
	uint32_t ktype = QM_STR;
	uint32_t vtype = QM_STR;
	uint32_t hd;

	int written = snprintf(buf, sizeof(buf), "%s", fname);
	if (written < 0 || (size_t) written >= sizeof(buf)) {
		fprintf(stderr, "Filename too long\n");
		exit(EXIT_FAILURE);
	}

	char *first_col = strchr(buf, ':'), *second_col;

	if (first_col) {
		*first_col = '\0';
		first_col++;
		second_col = strchr(first_col, ':');

		if (second_col) {
			*second_col = '\0';
			second_col++;
			vtype = _qmape_type(second_col);
		}

	if (!strcmp(first_col, "a")) {
		flags |= QM_AINDEX;
		ktype = QM_HNDL;
	} else if (*first_col == '2') {
		flags |= QM_MULTIVALUE | QM_SORTED;
		first_col++;
		if (*first_col)
			ktype = _qmape_type(first_col);
	} else
		ktype = _qmape_type(first_col);
	}

	/* Mask out CLI-specific flags before passing to library */
	hd = qmap_open(buf, "hd", ktype, vtype,
			QDBE_MASK, (flags & QM_FLAGS_MASK) | QM_MIRROR);
	metas[hd].types[0] = ktype;
	metas[hd].types[1] = vtype;
	metas[hd + 1].types[0] = vtype;
	metas[hd + 1].types[1] = ktype;
	return hd;
}

static void u_print(const void *data) {
	printf("%u", * (uint32_t *) data);
}

static void s_print(const void *data) {
	printf("%s", (char *) data);
}

/* ========================================================================
 * -Q recall-query CLI mode (dlopen'd axis plugins) — PLAN-REC-QUERY.md §4.2
 *
 * Entirely separate from the flat key/value CLI above: -Q must be argv[1],
 * every other flag here is long-only (getopt_long) and never touches
 * `optstr`/the two-pass flat-ops getopt loop in main(). qmap/libqmap link
 * only libqmap + libqsys; axis .so plugins are loaded via qsys_dlopen and
 * never linked (PLAN-REC-QUERY.md §1.2 dependency invariant).
 * ======================================================================== */

#define QMAP_RQ_DEFAULT_TOP 10
#define QMAP_RQ_ALL_TOP 65536  /* --top 0 ("all"): rec_query_run rejects
                                 * top_k == 0 (rec_axis.c), so the CLI
                                 * substitutes this fixed cap instead of
                                 * calling into the kernel with 0
                                 * (PLAN-REC-QUERY.md D18). */

enum {
	RQ_OPT_DL = 256,
	RQ_OPT_OPEN,
	RQ_OPT_AXIS,
	RQ_OPT_PARAMS,
	RQ_OPT_AND,
	RQ_OPT_OR,
	RQ_OPT_NOT,
	RQ_OPT_COMBINE,
	RQ_OPT_TOP,
	RQ_OPT_MIN,
	RQ_OPT_LIST_AXES,
	RQ_OPT_HELP
};

static const struct option rq_long_opts[] = {
	{ "dl",        required_argument, NULL, RQ_OPT_DL },
	{ "open",      required_argument, NULL, RQ_OPT_OPEN },
	{ "axis",      required_argument, NULL, RQ_OPT_AXIS },
	{ "params",    required_argument, NULL, RQ_OPT_PARAMS },
	{ "and",       no_argument,       NULL, RQ_OPT_AND },
	{ "or",        no_argument,       NULL, RQ_OPT_OR },
	{ "not",       no_argument,       NULL, RQ_OPT_NOT },
	{ "combine",   required_argument, NULL, RQ_OPT_COMBINE },
	{ "top",       required_argument, NULL, RQ_OPT_TOP },
	{ "min",       required_argument, NULL, RQ_OPT_MIN },
	{ "list-axes", no_argument,       NULL, RQ_OPT_LIST_AXES },
	{ "help",      no_argument,       NULL, RQ_OPT_HELP },
	{ NULL, 0, NULL, 0 }
};

static void
qmap_rq_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s -Q [--dl PATH [--open SPEC]]...\n", prog);
	fprintf(stderr, "          [[--and|--or|--not] --axis SLOT [--params STRING]]...\n");
	fprintf(stderr, "          [--combine and|or|not] [--top N] [--min SCORE] [--list-axes]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -Q must be the first argument. Loads axis plugin .so files via\n");
	fprintf(stderr, "    dlopen (never linked into qmap itself), builds a query from the\n");
	fprintf(stderr, "    --axis/--params/--and/--or/--not flags, and runs it.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "        --dl PATH        dlopen an axis plugin .so (repeatable)\n");
	fprintf(stderr, "        --open SPEC      call the .so just loaded by --dl's rec_axis_open(SPEC)\n");
	fprintf(stderr, "                         (if exported) and bind the result via rec_axis_set_ctx()\n");
	fprintf(stderr, "        --axis SLOT      add a registered axis slot to the query (repeatable)\n");
	fprintf(stderr, "        --params STRING  decode STRING via the axis's decode fn (or pass raw\n");
	fprintf(stderr, "                         if it has none); binds to the --axis just given\n");
	fprintf(stderr, "        --and/--or/--not sticky join for the NEXT --axis (ignored on the seed)\n");
	fprintf(stderr, "        --combine MODE   and|or|not: overrides every per-axis join after the seed\n");
	fprintf(stderr, "        --top N          max results (default %d; 0 = all, capped at %d)\n",
			QMAP_RQ_DEFAULT_TOP, QMAP_RQ_ALL_TOP);
	fprintf(stderr, "        --min SCORE      minimum score floor (default 0.0)\n");
	fprintf(stderr, "        --list-axes      print registered axes (slot/name/fill/rank/ctx) and exit\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    QMAP_AXIS_LIBS env var: colon-separated .so paths dlopen'd before flag\n");
	fprintf(stderr, "    parsing (dlopen-only, no ctx binding — useful for --list-axes discovery).\n");
}

static rec_join_t
qmap_rq_parse_join(const char *s, const char *flag)
{
	if (!strcmp(s, "and")) return REC_JOIN_AND;
	if (!strcmp(s, "or")) return REC_JOIN_OR;
	if (!strcmp(s, "not")) return REC_JOIN_NOT;
	fprintf(stderr, "-Q: invalid %s value '%s' (want and|or|not)\n", flag, s);
	exit(EXIT_FAILURE);
}

static void
qmap_rq_dlopen_env_one(const char *path)
{
	void *handle = qsys_dlopen(path, 0);
	if (!handle) {
		const char *err = qsys_dlerror();
		fprintf(stderr, "-Q: QMAP_AXIS_LIBS %s failed: %s\n",
				path, err ? err : "unknown error");
	}
}

/* Parses -Q's flags (argv/argc are main()'s original, unshifted arguments;
 * argv[1] == "-Q"). Runs entirely on getopt_long/long-only state, indepen-
 * dent of the flat-ops getopt loop below. */
static int
qmap_recall_query(int argc, char *argv[])
{
	rec_query_t q;
	memset(&q, 0, sizeof(q));
	q.top_k = QMAP_RQ_DEFAULT_TOP;

	rec_join_t pending_join = REC_JOIN_AND;
	int have_pending_join = 0;
	void *last_dl_handle = NULL;
	int last_dl_slot_before = 0;
	int have_last_dl = 0;
	int last_axis_idx = -1;
	int list_axes = 0;

	const char *env_libs = getenv("QMAP_AXIS_LIBS");
	if (env_libs && *env_libs) {
		char *copy = strdup(env_libs);
		if (!copy) {
			fprintf(stderr, "-Q: out of memory\n");
			return EXIT_FAILURE;
		}
		char *save = NULL;
		for (char *tok = strtok_r(copy, ":", &save); tok;
				tok = strtok_r(NULL, ":", &save))
			qmap_rq_dlopen_env_one(tok);
		free(copy);
	}

	optind = 2; /* skip argv[0] (prog) and argv[1] ("-Q") */
	int ch, option_index;
	while ((ch = getopt_long(argc, argv, "", rq_long_opts, &option_index)) != -1) {
		(void) option_index;
		switch (ch) {
		case RQ_OPT_DL:
			last_dl_slot_before = rec_axis_count();
			last_dl_handle = qsys_dlopen(optarg, 0);
			if (!last_dl_handle) {
				const char *err = qsys_dlerror();
				fprintf(stderr, "-Q: --dl %s failed: %s\n",
						optarg, err ? err : "unknown error");
				have_last_dl = 0;
				break;
			}
			have_last_dl = 1;
			break;
		case RQ_OPT_OPEN: {
			if (!have_last_dl) {
				fprintf(stderr, "-Q: --open with no preceding successful --dl, ignoring\n");
				break;
			}
			int slot_after = rec_axis_count();
			int n_new = slot_after - last_dl_slot_before;
			if (n_new < 1) {
				fprintf(stderr, "-Q: --open: plugin registered no axis, cannot bind ctx\n");
				break;
			}
			if (n_new > 1)
				fprintf(stderr,
					"-Q: --open: plugin registered %d axes; binding to the last one only\n",
					n_new);
			int new_slot = slot_after - 1;
			void *sym = qsys_dlsym(last_dl_handle, "rec_axis_open");
			if (!sym) {
				fprintf(stderr,
					"-Q: --open: plugin has no rec_axis_open symbol, axis %d left ctx-less\n",
					new_slot);
				break;
			}
			void *(*open_fn)(const char *);
			memcpy(&open_fn, &sym, sizeof(open_fn));
			void *ctx = open_fn(optarg);
			rec_axis_set_ctx(new_slot, ctx);
			break;
		}
		case RQ_OPT_AXIS: {
			if (q.n_axes >= REC_QUERY_MAX_AXES) {
				fprintf(stderr, "-Q: too many --axis (max %d)\n", REC_QUERY_MAX_AXES);
				return EXIT_FAILURE;
			}
			char *end;
			long slot = strtol(optarg, &end, 10);
			if (*end != '\0' || slot < 0 || slot >= REC_QUERY_MAX_AXES) {
				fprintf(stderr, "-Q: invalid --axis slot '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			last_axis_idx = q.n_axes;
			q.axes[last_axis_idx].slot = (int) slot;
			q.axes[last_axis_idx].params = NULL;
			q.axes[last_axis_idx].join = have_pending_join ? pending_join : REC_JOIN_AND;
			q.n_axes++;
			have_pending_join = 0;
			break;
		}
		case RQ_OPT_PARAMS: {
			if (last_axis_idx < 0) {
				fprintf(stderr, "-Q: --params with no preceding --axis, ignoring\n");
				break;
			}
			int slot = q.axes[last_axis_idx].slot;
			void *decoded = rec_axis_decode(slot, optarg);
			q.axes[last_axis_idx].params = decoded ? decoded : (void *) optarg;
			break;
		}
		case RQ_OPT_AND:
			pending_join = REC_JOIN_AND;
			have_pending_join = 1;
			break;
		case RQ_OPT_OR:
			pending_join = REC_JOIN_OR;
			have_pending_join = 1;
			break;
		case RQ_OPT_NOT:
			pending_join = REC_JOIN_NOT;
			have_pending_join = 1;
			break;
		case RQ_OPT_COMBINE:
			q.combine = qmap_rq_parse_join(optarg, "--combine");
			break;
		case RQ_OPT_TOP: {
			char *end;
			long top = strtol(optarg, &end, 10);
			if (*end != '\0' || top < 0) {
				fprintf(stderr, "-Q: invalid --top value '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			q.top_k = top == 0 ? QMAP_RQ_ALL_TOP : (size_t) top;
			break;
		}
		case RQ_OPT_MIN: {
			char *end;
			float min = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "-Q: invalid --min value '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			q.min_score = min;
			break;
		}
		case RQ_OPT_LIST_AXES:
			list_axes = 1;
			break;
		case RQ_OPT_HELP:
			qmap_rq_usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			qmap_rq_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (list_axes) {
		int n = rec_axis_count();
		printf("slot  name              fill  rank  ctx\n");
		for (int slot = 0; slot < n; slot++) {
			const rec_axis_t *axis = rec_axis_get(slot);
			if (!axis)
				continue;
			printf("%-4d  %-16s  %-4s  %-4s  %s\n",
					slot, axis->name,
					axis->fill ? "y" : "n",
					axis->rank ? "y" : "n",
					axis->ctx ? "y" : "n");
		}
		return EXIT_SUCCESS;
	}

	if (q.n_axes == 0) {
		fprintf(stderr, "-Q: no --axis given (use --list-axes to inspect loaded plugins)\n");
		return EXIT_FAILURE;
	}

	rec_ref_t *refs = malloc(q.top_k * sizeof(*refs));
	float *scores = malloc(q.top_k * sizeof(*scores));
	if (!refs || !scores) {
		fprintf(stderr, "-Q: out of memory\n");
		free(refs);
		free(scores);
		return EXIT_FAILURE;
	}

	int have_rank = 0;
	for (int i = 0; i < q.n_axes; i++) {
		const rec_axis_t *axis = rec_axis_get(q.axes[i].slot);
		if (axis && axis->rank) {
			have_rank = 1;
			break;
		}
	}
	int pure_filter = !have_rank && !q.consumer_score;

	int n = rec_query_run(&q, refs, pure_filter ? NULL : scores);
	if (n < 0) {
		fprintf(stderr, "-Q: rec_query_run failed\n");
		free(refs);
		free(scores);
		return EXIT_FAILURE;
	}

	for (int i = 0; i < n; i++) {
		if (pure_filter)
			printf("%llu\n", (unsigned long long) refs[i]);
		else
			printf("%llu %f\n", (unsigned long long) refs[i], scores[i]);
	}

	free(refs);
	free(scores);
	return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{
	if (argc >= 2 && !strcmp(argv[1], "-Q"))
		return qmap_recall_query(argc, argv);

	static char *optstr = "kxla:q:p:d:D:g:m:c:rR:L:?";
	char *fname = NULL, ch;
	uint32_t flags = QH_RDONLY, aux;

	if (argc < 2) {
		usage(*argv);
		return EXIT_FAILURE;
	}

	types[QM_HNDL].print = u_print;
	types[QM_STR].print = s_print;
	types[QM_U32].print = u_print;

	aqs[AQ_A].hd = qmap_open(NULL, NULL,
			QM_HNDL, QM_HNDL,
			QDBE_QMASK, QM_AINDEX);

	aqs[AQ_Q].hd = qmap_open(NULL, NULL,
			QM_HNDL, QM_HNDL,
			QDBE_QMASK, QM_AINDEX);

	while ((ch = getopt(argc, argv, optstr)) != -1)
		switch (ch) {
		case 'a':
			aux_hd = gen_open(optarg, QH_RDONLY);
			aux = aqs[AQ_A].hd;
			qmap_put(aux, NULL, &aux_hd);
			aqs[AQ_A].n++;
			break;
		case 'q':
			aux_hd = gen_open(optarg, QH_RDONLY);
			aux = aqs[AQ_Q].hd;
			qmap_put(aux, NULL, &aux_hd);
			aqs[AQ_Q].n++;
			break;
		case 'x':
			bail = 1;
			break;
		case 'k':
			print_keys = 1;
			break;
	case 'p':
	case 'd':
	case 'D':
		/* TODO m1 can be inferred */
		flags &= ~QH_RDONLY;
	case 'l':
	case 'L':
	case 'R':
	case 'g':
	case 'm':
	case 'c':
	case 'r': break;
		default: usage(*argv); return EXIT_FAILURE;
		case '?': usage(*argv); return EXIT_SUCCESS;
		}

	if (optind >= argc) {
		usage(*argv);
		return EXIT_FAILURE;
	}
	fname = argv[optind];

	optind = 1;
	prim_hd = gen_open(fname, flags);
	srand(time(NULL));

	while ((ch = getopt(argc, argv, optstr)) != -1) switch (ch) {
	case 'R': gen_rand(); break;
	case 'L': gen_list_missing(); break;
	case 'l': gen_list(); break;
	case 'p': gen_put(); break;
	case 'd': gen_del(); break;
	case 'D': gen_del_all(); break;
	case 'g': gen_get(optarg); break;
	case 'm': gen_multi(); break;
	case 'c': gen_count(); break;
	case 'r': reverse = !reverse; break;
	}
}

/**
 * @page qmap qmap(1)
 * @brief Qmap command-line tool — create, query, and associate persistent maps.
 *
 * CLI simples para a engine Qmap. Substitui o antigo **qdb** e usa diretamente a libqmap.
 *
 * ## Overview
 * Cria/abre bases chave-valor em disco ou memória; permite consultas, associações, espelhos e iteração.
 * Suporta tipos *unsigned* e *string*. **Auto-index só é ativado se o tipo de chave for `a`.**
 *
 * ## Instalação
 * Ver: <https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages>
 *
 * ## Usage
 * ```
 * qmap [-qa ARG] [[-rl] [-Rpdg ARG] ...] file[[:k]:v]
 * ```
 * Executa `qmap -?` para a ajuda completa.
 *
 * ### Options
 * - **-r**
 *   Inverte a direção (troca chave/valor nas operações).
 * - **-l**
 *   Lista todos os pares.
 * - **-L**
 *   Lista “missing” (**requer `-q`** para base correspondente).
 * - **-q** *file[:k[:v]]*
 *   Base para *string lookups* e impressão (cadeia de consulta).
 * - **-a** *file[:k[:v]]*
 *   Base para *reversed string lookups* e impressão associada.
 * - **-R** *KEY*
 *   Valor aleatório para *KEY* (`.` = qualquer).
 * - **-p** *KEY[:VAL]*
 *   Insere/atualiza par.
 * - **-d** *KEY[:VAL]*
 *   Apaga par(es).
 * - **-g** *KEY*
 *   Obtém valor(es) de uma chave (`.` = todas).
 * - **-x**
 *   Em impressão de associações, pára no primeiro resultado.
 * - **-k**
 *   Também imprime as chaves (em `-g` e `-R`).
 *
 * ### Type specifiers
 * - **u** — unsigned integer
 * - **s** — string
 * - **a** — só chave: unsigned com auto-index (default **não** é `a`)
 *
 * ### Exemplos
 * @code
 * # IDs automáticos: chave 'a', valores string
 * qmap -p Mathew owners.db:a:s              # → id do Mathew
 * qmap -p cat    pets.db:a:s                # → id do 'cat'
 *
 * # Associação (sem duplicados)
 * qmap -p 1:1 assoc.db:u:u                  # owner_id:pet_id
 *
 * # Ver o animal do Mathew (usa -q/-a para resolver nomes/ids)
 * qmap -q owners.db:a:s -a pets.db:a:s -g Mathew assoc.db:u:u
 *
 * # Aleatório para uma KEY
 * qmap -q owners.db:a:s -a pets.db:a:s -R Mathew assoc.db:u:u
 * @endcode
 *
 * ### Notes
 * - `-r` é contra-intuitivo: ligado, procuras **pelas chaves primárias**.
 * - Só grava no fim se a base principal não estiver em read-only (i.e., usaste `-p`/`-d`).
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
#include <ttypt/qsys.h>

#define DEBUG_LVL 0
#define DEBUG(lvl, ...) \
	if (DEBUG_LVL > lvl) WARN(__VA_ARGS__)

#define QM_MAX 1024
#define QDBE_MASK (32768 - 1)
#define QDBE_QMASK 0xF
#define QH_RDONLY 0x32

typedef struct {
	unsigned types[2];
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

unsigned QH_NOT_NEW = 1;

unsigned prim_hd, aux_hd;

const void *value_ptr, *key_ptr;
char *col;

typedef struct {
	unsigned hd, n;
} aq_t;

enum aq {
	AQ_A,
	AQ_Q,
};

aq_t aqs[2];

unsigned reverse = 0, bail = 0, print_keys = 0;

unsigned qmap_get_type;
const void **qmap_get_ptr;

void qmape_print(unsigned hd, enum qmape_mbr t,
		const void *buf)
{
	qmape_meta_t *meta = &metas[hd];
	qmape_type_t *type = &types[meta->types[t]];
	type->print(buf);
}

void
usage(char *prog)
{
	fprintf(stderr, "Usage: %s [-qa ARG] [[-rl] [-Rpdg ARG] ...] file[[:k]:v]", prog);
	fprintf(stderr, "    Options:\n");
	fprintf(stderr, "        -r               reverse operation\n");
	fprintf(stderr, "        -l               list all values\n");
	fprintf(stderr, "        -L               list missing values\n");
	fprintf(stderr, "        -q file[:k[:v]]  db to use string lookups and printing\n");
	fprintf(stderr, "        -a file[:k[:v]]  db to use for reversed string lookups and printing\n");
	fprintf(stderr, "        -R KEY           get random value of key (empty key for any)\n");
	fprintf(stderr, "        -p KEY[:VAL]     put a key/value pair\n");
	fprintf(stderr, "        -d KEY[:VAL]     delete key/value pair(s)\n");
	fprintf(stderr, "        -g KEY           get value(s) of a key\n");
	fprintf(stderr, "        -x               when printing associations, bail on first result\n");
	fprintf(stderr, "        -k               also print keys (for get and rand).\n");
	fprintf(stderr, "    'k' and 'v' are key and value types. Supported values:\n");
	fprintf(stderr, "         u               unsigned\n");
	fprintf(stderr, "         s               string (default value type)\n");
	fprintf(stderr, "         a               key only! unsigned automatic index (default)\n");
	fprintf(stderr, "         2<base-type>    key only! allows duplicates\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Use '.' as the KEY for all keys!\n");
}

static inline unsigned
qmape_type(unsigned phd, enum qmape_mbr t, unsigned reverse)
{
	qmape_meta_t *meta = &metas[phd];
	return meta->types[reverse ? !t : t];
}

static inline const void *rec_query(
		enum aq aq,
		const void *tbuf,
		const void *buf,
		unsigned tmprev)
{
	tmprev = (aqs[AQ_Q].n & 1) == tmprev;
	unsigned c2 = qmap_iter(aqs[aq].hd, NULL, 0);
	ids_t rqs = ids_init();
	unsigned aux_hd;
	const void *key, *value, *aux2;

	unsigned lktype = qmape_type(prim_hd,
			KEY, !reverse);

	while (qmap_next(&key, &value, c2)) {
		aux_hd = * (unsigned *) value;

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

	while ((aux_hd = ids_pop(&rqs)) != (unsigned) -1) {
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
	unsigned aq_hd = aqs[AQ_Q].hd;
	unsigned c = qmap_iter(aq_hd, NULL, 0);
	unsigned rev = !reverse;
	unsigned type = qmape_type(prim_hd,
			is_value ? KEY : VALUE,
			rev);
	const void *key, *value;

	while (qmap_next(&key, &value, c)) {
		rev = !rev;
		unsigned aux_hd = * (unsigned *)
			value;
		type = qmape_type(aux_hd, KEY, rev);
	}

	return type == QM_STR;
}

inline static const void *
_gen_lookup(const void **buf, unsigned *uret, char *str,
		enum aq aq, int is_value)
{
	unsigned cond = gen_cond(is_value);
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
	static unsigned aret, bret;

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
	unsigned aux_hd;
	unsigned aq_hd = aqs[AQ_A].hd;
	unsigned c2 = qmap_iter(aq_hd, NULL, 0);
	const void *key, *value;

	while (qmap_next(&key, &value, c2)) {
		aux_hd = * (unsigned *)
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
	unsigned count = 0, rand;
	unsigned c;
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

	rand = random() % count;
	c = qmap_iter(prim_hd + !reverse, iter_key, 0);

	while (qmap_next(&key_ptr, &value_ptr, c))
		if (!assoc_exists(key_ptr))
			continue;
		else if ((--count) <= rand) {
			qmap_fin(c);
			break;
		}

	_gen_get();
}

static void gen_get(char *str) {
	const void *iter_key = gen_lookup(str);
	unsigned c;
	unsigned nonce = 1, hd;
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
	unsigned c;
	unsigned cond, aux;

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

static inline void gen_put(void) {
	unsigned id;
	const void *key;

	gen_lookup(optarg);

	key = col ? key_ptr : NULL;
	id = qmap_put(prim_hd, key, value_ptr);
	qmape_print(prim_hd, KEY,
			key ? key : (char *) &id);
	putchar('\n');
}

static inline void gen_list_missing(void) {
	unsigned c;
	gen_lookup(NULL);

	if (!aqs[AQ_Q].n) {
		fprintf(stderr, "list missing needs a corresponding database\n");
		return;
	}

	c = qmap_iter(prim_hd + !reverse, NULL, 0);
	while (qmap_next(&key_ptr, &value_ptr, c)) {
		unsigned aqs_hd = aqs[AQ_Q].hd;
		unsigned c2 = qmap_iter(aqs_hd, NULL, 0);
		const void *skey, *sval;

		while (qmap_next(&skey, &sval, c2)) {
			unsigned ahd = (* (unsigned *)
				sval)
				+ !reverse;
			key_ptr = qmap_get(ahd, value_ptr);
			if (key_ptr)
				_gen_get();
		}
	}
}

unsigned _qmape_type(char *which) {
	if (*which == 's')
		return QM_STR;
	else
		return QM_HNDL;
}

unsigned gen_open(char *fname, unsigned flags) {
	char buf[BUFSIZ];
	unsigned ktype = QM_STR;
	unsigned vtype = QM_STR;
	unsigned hd;

	strcpy(buf, fname);

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
		/*
		} else if (*first_col == '2') {
			flags |= QH_DUP;
			first_col++;
			if (*first_col)
				key_type = first_col;
		*/
		} else
			ktype = _qmape_type(first_col);
	}

	hd = qmap_open(buf, "hd", ktype, vtype,
			QDBE_MASK, flags | QM_MIRROR);
	metas[hd].types[0] = ktype;
	metas[hd].types[1] = vtype;
	metas[hd + 1].types[0] = vtype;
	metas[hd + 1].types[1] = ktype;
	return hd;
}

static void u_print(const void *data) {
	printf("%u", * (unsigned *) data);
}

static void s_print(const void *data) {
	printf("%s", (char *) data);
}

int
main(int argc, char *argv[])
{
	static char *optstr = "kxla:q:p:d:g:rR:L:?";
	char *fname = argv[argc - 1], ch;
	unsigned flags = QH_RDONLY, aux;

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
			/* TODO m1 can be inferred */
			flags &= ~QH_RDONLY;
		case 'l':
		case 'L':
		case 'R':
		case 'g':
		case 'r': break;
		default: usage(*argv); return EXIT_FAILURE;
		case '?': usage(*argv); return EXIT_SUCCESS;
		}

	optind = 1;
	prim_hd = gen_open(fname, flags);
	srandom(time(NULL));

	while ((ch = getopt(argc, argv, optstr)) != -1) switch (ch) {
	case 'R': gen_rand(); break;
	case 'L': gen_list_missing(); break;
	case 'l': gen_list(); break;
	case 'p': gen_put(); break;
	case 'd': gen_del(); break;
	case 'g': gen_get(optarg); break;
	case 'r': reverse = !reverse; break;
	}

	if (!(flags & QH_RDONLY))
		qmap_save();
}

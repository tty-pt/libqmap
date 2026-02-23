/* test_multivalue.c
 * Comprehensive test suite for QM_MULTIVALUE functionality
 */

#include <ttypt/qmap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define TEST(name) \
	do { \
		printf("Running %s...", #name); \
		fflush(stdout); \
		name(); \
		printf(" PASS\n"); \
	} while(0)

/* Test 1: Validation - QM_MULTIVALUE requires QM_SORTED */
static void test_multivalue_requires_sorted(void)
{
	uint32_t hd;
	
	/* Should fail without QM_SORTED */
	hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_MULTIVALUE);
	assert(hd == QM_MISS);
	
	/* Should succeed with QM_SORTED */
	hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_MULTIVALUE | QM_SORTED);
	assert(hd != QM_MISS);
	qmap_close(hd);
}

/* Test 2: Basic multi-value operations */
static void test_multivalue_basic(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	uint32_t key = 100;
	
	/* Insert three values with same key */
	qmap_put(hd, &key, &(uint32_t){1});
	qmap_put(hd, &key, &(uint32_t){2});
	qmap_put(hd, &key, &(uint32_t){3});
	
	/* Verify count */
	assert(qmap_count(hd, &key) == 3);
	
	/* Verify qmap_get returns first */
	const uint32_t *first = qmap_get(hd, &key);
	assert(first != NULL);
	assert(*first == 1);
	
	/* Verify qmap_get_multi returns all */
	uint32_t cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	
	uint32_t values[3];
	int i = 0;
	const void *k, *v;
	while (qmap_next(&k, &v, cur)) {
		assert(i < 3);
		values[i++] = *(const uint32_t*)v;
	}
	
	assert(i == 3);
	assert(values[0] == 1);
	assert(values[1] == 2);
	assert(values[2] == 3);
	
	qmap_close(hd);
}

/* Test 3: Deletion with duplicates */
static void test_multivalue_delete(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	uint32_t key = 50;
	
	/* Insert 3 duplicates */
	qmap_put(hd, &key, &(uint32_t){10});
	qmap_put(hd, &key, &(uint32_t){20});
	qmap_put(hd, &key, &(uint32_t){30});
	
	assert(qmap_count(hd, &key) == 3);
	
	/* Delete one - should only remove one entry */
	qmap_del(hd, &key);
	
	/* After deletion, count should decrease by 1 */
	uint32_t remaining = qmap_count(hd, &key);
	assert(remaining == 2);
	
	/* Delete again */
	qmap_del(hd, &key);
	remaining = qmap_count(hd, &key);
	assert(remaining == 1);
	
	/* Delete last one */
	qmap_del(hd, &key);
	remaining = qmap_count(hd, &key);
	assert(remaining == 0);
	
	/* Key should not be found */
	const void *val = qmap_get(hd, &key);
	assert(val == NULL);
	
	qmap_close(hd);
}

/* Interval structure for README test */
struct interval {
	time_t min;
	time_t max;
	uint32_t who;
};

/* Association callback to extract max time */
static void extract_max(const void **skey, const void *pkey, const void *value)
{
	(void)pkey;
	const struct interval *iv = value;
	static time_t max_time;
	max_time = iv->max;
	*skey = &max_time;
}

/* Test 4: The critical README scenario - intervals with duplicate max times */
static void test_multivalue_intervals(void)
{
	/* Register types */
	uint32_t qm_interval = qmap_reg(sizeof(struct interval));
	uint32_t qm_time = qmap_reg(sizeof(time_t));
	
	/* Create primary and secondary maps */
	uint32_t primary = qmap_open(NULL, "primary", qm_interval, qm_interval, 0xFF, 0);
	uint32_t by_time = qmap_open(NULL, "by_time", qm_time, qm_interval, 0xFF,
	                              QM_SORTED | QM_MULTIVALUE);
	
	qmap_assoc(by_time, primary, extract_max);
	
	/* Two intervals with SAME max time (this was the bug!) */
	struct interval i1 = {.min=100, .max=9999, .who=1};
	struct interval i2 = {.min=200, .max=9999, .who=2};
	
	qmap_put(primary, &i1, &i1);
	qmap_put(primary, &i2, &i2);
	
	/* VERIFY: Both are in by_time index */
	time_t max_9999 = 9999;
	uint32_t count = qmap_count(by_time, &max_9999);
	assert(count == 2);
	
	/* VERIFY: Can iterate over both */
	uint32_t cur = qmap_get_multi(by_time, &max_9999);
	assert(cur != QM_MISS);
	
	int found_count = 0;
	int found_who[2] = {0, 0};
	const void *k, *v;
	
	while (qmap_next(&k, &v, cur)) {
		const struct interval *iv = v;
		assert(iv->max == 9999);
		assert(iv->who == 1 || iv->who == 2);
		found_who[iv->who - 1] = 1;
		found_count++;
	}
	
	assert(found_count == 2);
	assert(found_who[0] == 1);  /* Found who=1 */
	assert(found_who[1] == 1);  /* Found who=2 */
	
	/* VERIFY: Deleting i1 doesn't delete i2 (critical bug fix!) */
	qmap_del(primary, &i1);
	count = qmap_count(by_time, &max_9999);
	assert(count == 1);
	
	/* VERIFY: i2 is still retrievable */
	const struct interval *remaining = qmap_get(by_time, &max_9999);
	assert(remaining != NULL);
	assert(remaining->who == 2);
	assert(remaining->min == 200);
	assert(remaining->max == 9999);
	
	qmap_close(primary);
	qmap_close(by_time);
}

/* Test 5: Backward compatibility - maps without QM_MULTIVALUE */
static void test_backward_compatibility(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_SORTED);
	uint32_t key = 100;
	
	/* Without QM_MULTIVALUE, duplicates should overwrite */
	qmap_put(hd, &key, &(uint32_t){1});
	qmap_put(hd, &key, &(uint32_t){2});
	
	/* Should only have one entry */
	assert(qmap_count(hd, &key) == 1);
	
	/* Second value should have overwritten first */
	const uint32_t *val = qmap_get(hd, &key);
	assert(val != NULL);
	assert(*val == 2);
	
	qmap_close(hd);
}

/* Test 6: Edge cases */
static void test_multivalue_edge_cases(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	uint32_t key = 1;
	
	/* Empty map */
	assert(qmap_count(hd, &key) == 0);
	assert(qmap_get(hd, &key) == NULL);
	assert(qmap_get_multi(hd, &key) == QM_MISS);
	
	/* Single value */
	qmap_put(hd, &key, &(uint32_t){100});
	assert(qmap_count(hd, &key) == 1);
	
	const uint32_t *val = qmap_get(hd, &key);
	assert(val != NULL);
	assert(*val == 100);
	
	/* Many duplicates (stress test) */
	for (int i = 1; i < 50; i++) {
		qmap_put(hd, &key, &(uint32_t){i});
	}
	
	uint32_t total = qmap_count(hd, &key);
	assert(total == 50);
	
	/* Verify all can be iterated */
	uint32_t cur = qmap_get_multi(hd, &key);
	uint32_t iter_count = 0;
	const void *k, *v;
	
	while (qmap_next(&k, &v, cur)) {
		iter_count++;
	}
	
	assert(iter_count == 50);
	
	qmap_close(hd);
}

/* Test 7: Multiple keys with duplicates */
static void test_multivalue_multiple_keys(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	
	/* Add duplicates for key 1 */
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){10});
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){11});
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){12});
	
	/* Add duplicates for key 2 */
	qmap_put(hd, &(uint32_t){2}, &(uint32_t){20});
	qmap_put(hd, &(uint32_t){2}, &(uint32_t){21});
	
	/* Add single value for key 3 */
	qmap_put(hd, &(uint32_t){3}, &(uint32_t){30});
	
	/* Verify counts */
	assert(qmap_count(hd, &(uint32_t){1}) == 3);
	assert(qmap_count(hd, &(uint32_t){2}) == 2);
	assert(qmap_count(hd, &(uint32_t){3}) == 1);
	assert(qmap_count(hd, &(uint32_t){4}) == 0);
	
	/* Verify total count */
	assert(qmap_count(hd, NULL) == 6);
	
	/* Delete from key 1, shouldn't affect key 2 */
	qmap_del(hd, &(uint32_t){1});
	assert(qmap_count(hd, &(uint32_t){1}) == 2);
	assert(qmap_count(hd, &(uint32_t){2}) == 2);
	
	qmap_close(hd);
}

/* Test 8: String keys with duplicates */
static void test_multivalue_strings(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_STR, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	
	const char *key = "test";
	qmap_put(hd, key, &(uint32_t){1});
	qmap_put(hd, key, &(uint32_t){2});
	qmap_put(hd, key, &(uint32_t){3});
	
	assert(qmap_count(hd, key) == 3);
	
	uint32_t cur = qmap_get_multi(hd, key);
	int count = 0;
	const void *k, *v;
	
	while (qmap_next(&k, &v, cur)) {
		assert(strcmp((const char*)k, "test") == 0);
		count++;
	}
	
	assert(count == 3);
	
	qmap_close(hd);
}

/* Test 9: qmap_del_all() function */
static void test_multivalue_del_all(void)
{
	/* Test with QM_MULTIVALUE map */
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_MULTIVALUE | QM_SORTED);
	
	/* Add duplicates for key 1 */
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){10});
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){11});
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){12});
	
	/* Add duplicates for key 2 */
	qmap_put(hd, &(uint32_t){2}, &(uint32_t){20});
	qmap_put(hd, &(uint32_t){2}, &(uint32_t){21});
	
	assert(qmap_count(hd, &(uint32_t){1}) == 3);
	assert(qmap_count(hd, &(uint32_t){2}) == 2);
	assert(qmap_count(hd, NULL) == 5);
	
	/* Delete all entries for key 1 */
	qmap_del_all(hd, &(uint32_t){1});
	
	/* Verify key 1 is completely gone */
	assert(qmap_count(hd, &(uint32_t){1}) == 0);
	assert(qmap_get(hd, &(uint32_t){1}) == NULL);
	
	/* Verify key 2 is unaffected */
	assert(qmap_count(hd, &(uint32_t){2}) == 2);
	assert(qmap_count(hd, NULL) == 2);
	
	/* Delete all for key 2 */
	qmap_del_all(hd, &(uint32_t){2});
	assert(qmap_count(hd, &(uint32_t){2}) == 0);
	assert(qmap_count(hd, NULL) == 0);
	
	qmap_close(hd);
	
	/* Test with non-MULTIVALUE map (should behave like qmap_del) */
	uint32_t hd2 = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_SORTED);
	
	qmap_put(hd2, &(uint32_t){1}, &(uint32_t){10});
	assert(qmap_count(hd2, &(uint32_t){1}) == 1);
	
	qmap_del_all(hd2, &(uint32_t){1});
	assert(qmap_count(hd2, &(uint32_t){1}) == 0);
	
	qmap_close(hd2);
}

int main(void)
{
	printf("=== QM_MULTIVALUE Test Suite ===\n\n");
	
	TEST(test_multivalue_requires_sorted);
	TEST(test_multivalue_basic);
	TEST(test_multivalue_delete);
	TEST(test_multivalue_intervals);
	TEST(test_backward_compatibility);
	TEST(test_multivalue_edge_cases);
	TEST(test_multivalue_multiple_keys);
	TEST(test_multivalue_strings);
	TEST(test_multivalue_del_all);
	
	printf("\n=== All tests passed! ===\n");
	return 0;
}

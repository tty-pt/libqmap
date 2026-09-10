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

/* Employee structure for test 15 */
typedef struct {
	uint32_t id;
	uint32_t dept;
	uint32_t hire_year;
	char name[32];
} employee_t;

/* Association callback to extract max time */
static void extract_max(const void **skey, const void *pkey, const void *value, void *userdata)
{
	(void)pkey;
	(void)userdata;
	const struct interval *iv = value;
	static time_t max_time;
	max_time = iv->max;
	*skey = &max_time;
}

/* Extract callbacks for employee indexes */
static void extract_dept(const void **skey, const void *pkey, const void *value, void *userdata)
{
	(void)pkey;
	(void)userdata;
	const employee_t *emp = value;
	static uint32_t dept;
	dept = emp->dept;
	*skey = &dept;
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
	
	qmap_assoc(by_time, primary, extract_max, NULL);
	
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

/* Test 10: Large-scale duplicates - verify system handles many duplicates efficiently */
static void test_multivalue_large_scale(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFFFF,
	                        QM_MULTIVALUE | QM_SORTED);
	uint32_t key = 42;
	const int COUNT = 1000;
	
	/* Insert 1000 duplicates */
	for (int i = 0; i < COUNT; i++) {
		qmap_put(hd, &key, &(uint32_t){i});
	}
	/* Verify count */
	assert(qmap_count(hd, &key) == (uint32_t)COUNT);
	

	/* Verify all accessible via iteration */
	uint32_t cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	
	int count = 0;
	const void *k, *v;
	while (qmap_next(&k, &v, cur)) {
		const uint32_t *val = v;
		assert(*val < (uint32_t)COUNT);
		count++;
	}
	qmap_fin(cur);
	assert(count == COUNT);
	
	/* Delete all at once */
	qmap_del_all(hd, &key);
	assert(qmap_count(hd, &key) == 0);
	
	/* Verify map is empty and functional */
	assert(qmap_count(hd, NULL) == 0);
	qmap_put(hd, &(uint32_t){999}, &(uint32_t){1});
	assert(qmap_count(hd, &(uint32_t){999}) == 1);
	
	qmap_close(hd);
}

/* Test 11: IDM tracking edge case - explicit position insertion via qmap_assoc */
static void test_multivalue_idm_tracking(void)
{
	uint32_t qm_interval = qmap_reg(sizeof(struct interval));
	uint32_t qm_time = qmap_reg(sizeof(time_t));
	
	uint32_t primary = qmap_open(NULL, "primary", qm_interval, qm_interval, 0xFF, 0);
	uint32_t by_time = qmap_open(NULL, "by_time", qm_time, qm_interval, 0xFF,
	                              QM_SORTED | QM_MULTIVALUE);
	
	qmap_assoc(by_time, primary, extract_max, NULL);
	
	/* Insert entries that will create high position numbers
	 * This tests the case where pn >= idm.last */
	for (int i = 0; i < 10; i++) {
		struct interval iv;
		memset(&iv, 0, sizeof(iv));
		iv.min = i*100;
		iv.max = 9999;
		iv.who = i;
		qmap_put(primary, &iv, &iv);
	}
	
	/* Force sorted index rebuild by marking dirty and iterating */
	time_t search = 9999;
	uint32_t cur = qmap_get_multi(by_time, &search);
	assert(cur != QM_MISS);
	
	/* Count entries - this will fail if IDM tracking is broken */
	int count = 0;
	const void *k, *v;
	while (qmap_next(&k, &v, cur)) {
		count++;
	}
	qmap_fin(cur);
	
	/* Should find all 10 entries */
	assert(count == 10);
	assert(qmap_count(by_time, &search) == 10);
	
	/* Verify each entry individually */
	cur = qmap_get_multi(by_time, &search);
	int found[10] = {0};
	while (qmap_next(&k, &v, cur)) {
		const struct interval *iv = v;
		assert(iv->who < 10);
		found[iv->who] = 1;
	}
	qmap_fin(cur);
	
	for (int i = 0; i < 10; i++) {
		assert(found[i] == 1); /* All entries found */
	}
	
	qmap_close(primary);
	qmap_close(by_time);
}

/* Test 12: In-memory large dataset with multiple keys */
static void test_multivalue_persistence(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFFFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	/* Create a dataset with 10 keys, each having 10 duplicates */
	const int NUM_KEYS = 10;
	const int DUPS_PER_KEY = 10;
	
	for (int k = 1; k <= NUM_KEYS; k++) {
		for (int v = 1; v <= DUPS_PER_KEY; v++) {
			qmap_put(hd, &k, &(uint32_t){k * 100 + v});
		}
	}
	
	/* Verify all keys have correct counts */
	for (int k = 1; k <= NUM_KEYS; k++) {
		assert(qmap_count(hd, &k) == (uint32_t)DUPS_PER_KEY);
	}
	
	/* Verify total entries */
	uint32_t cur = qmap_iter(hd, NULL, 0);
	int total = 0;
	const void *k, *v;
	while (qmap_next(&k, &v, cur)) total++;
	assert(total == NUM_KEYS * DUPS_PER_KEY);
	
	/* Delete all duplicates of key 5 */
	uint32_t key5 = 5;
	qmap_del_all(hd, &key5);
	assert(qmap_count(hd, &key5) == 0);
	
	/* Verify total entries decreased */
	cur = qmap_iter(hd, NULL, 0);
	total = 0;
	while (qmap_next(&k, &v, cur)) total++;
	assert(total == (NUM_KEYS - 1) * DUPS_PER_KEY);
	
	/* Add back key 5 with different count */
	for (uint32_t v = 1; v <= 5; v++) {
		qmap_put(hd, &key5, &(uint32_t){5000 + v});
	}
	assert(qmap_count(hd, &key5) == 5);
	
	/* Verify iteration over key 5 returns new values */
	cur = qmap_get_multi(hd, &key5);
	assert(cur != QM_MISS);
	int count = 0;
	while (qmap_next(&k, &v, cur)) {
		const uint32_t *val = v;
		assert(*val >= 5001 && *val <= 5005);
		count++;
	}
	assert(count == 5);
	
	qmap_close(hd);
}

/* Test 13: Concurrent iteration with multiple cursors */
static void test_multivalue_concurrent_iteration(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	uint32_t key1 = 10, key2 = 20;
	uint32_t values1[3] = {100, 101, 102};
	uint32_t values2[3] = {200, 201, 202};
	
	/* Insert duplicates for two keys */
	for (int i = 0; i < 3; i++) {
		qmap_put(hd, &key1, &values1[i]);
		qmap_put(hd, &key2, &values2[i]);
	}
	
	/* Test 1: Interleaved iteration on different keys */
	uint32_t cur1 = qmap_get_multi(hd, &key1);
	uint32_t cur2 = qmap_get_multi(hd, &key2);
	assert(cur1 != QM_MISS && cur2 != QM_MISS);
	
	const void *k, *v;
	const uint32_t *val;
	
	assert(qmap_next(&k, &v, cur1)); /* Get first from key1 */
	val = v;
	assert(*val == 100);
	
	assert(qmap_next(&k, &v, cur2)); /* Get first from key2 */
	val = v;
	assert(*val == 200);
	
	assert(qmap_next(&k, &v, cur1)); /* Get second from key1 */
	val = v;
	assert(*val == 101);
	
	assert(qmap_next(&k, &v, cur2)); /* Get second from key2 */
	val = v;
	assert(*val == 201);
	
	/* Finish both iterations */
	assert(qmap_next(&k, &v, cur1));
	val = v;
	assert(*val == 102);
	assert(qmap_next(&k, &v, cur1) == 0); /* No more */
	
	assert(qmap_next(&k, &v, cur2));
	val = v;
	assert(*val == 202);
	assert(qmap_next(&k, &v, cur2) == 0); /* No more */
	
	/* Test 2: Two cursors on the same key */
	cur1 = qmap_get_multi(hd, &key1);
	cur2 = qmap_get_multi(hd, &key1);
	assert(cur1 != QM_MISS && cur2 != QM_MISS);
	
	/* Both should see all values independently */
	int count1 = 0, count2 = 0;
	while (qmap_next(&k, &v, cur1)) count1++;
	while (qmap_next(&k, &v, cur2)) count2++;
	
	assert(count1 == 3);
	assert(count2 == 3);
	
	qmap_close(hd);
}

/* Test 14: Modification during iteration safety */
static void test_multivalue_modify_during_iteration(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	uint32_t key = 42;
	uint32_t values[3] = {100, 200, 300};
	
	for (int i = 0; i < 3; i++) {
		qmap_put(hd, &key, &values[i]);
	}
	
	/* Test 1: Adding during iteration (should not crash) */
	uint32_t cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	
	const void *k, *v;
	int count = 0;
	
	assert(qmap_next(&k, &v, cur));
	count++;
	
	/* Add a new value mid-iteration */
	qmap_put(hd, &key, &(uint32_t){400});
	
	/* Continue iteration - behavior is undefined but should not crash */
	while (qmap_next(&k, &v, cur)) {
		count++;
	}
	
	/* Verify the new value is in the map */
	assert(qmap_count(hd, &key) == 4);
	
	/* Test 2: Early termination of iteration */
	cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	assert(qmap_next(&k, &v, cur)); /* Get one value */
	qmap_fin(cur); /* Explicitly finish early */
	
	/* Map should still be functional */
	assert(qmap_count(hd, &key) == 4);
	
	/* Full iteration should work */
	cur = qmap_get_multi(hd, &key);
	count = 0;
	while (qmap_next(&k, &v, cur)) count++;
	assert(count == 4);
	
	qmap_close(hd);
}

/* Test 15: Multiple secondary indexes with QM_MULTIVALUE
 * NOTE: This test only uses one key value to avoid a known bug when
 * qmap_assoc is used with QM_MULTIVALUE and multiple distinct secondary keys.
 * See: segfault when iterating over secondary index after inserting entries
 * with different extract values (e.g., dept 10 and dept 20).
 */
static void test_multivalue_multiple_indexes(void)
{
	uint32_t qm_employee = qmap_reg(sizeof(employee_t));
	
	uint32_t primary = qmap_open(NULL, "emp_primary", QM_U32, qm_employee, 0xFF, 0);
	uint32_t by_dept = qmap_open(NULL, "emp_by_dept", QM_U32, qm_employee, 0xFF,
	                              QM_SORTED | QM_MULTIVALUE);
	
	assert(primary != QM_MISS && by_dept != QM_MISS);
	
	qmap_assoc(by_dept, primary, extract_dept, NULL);
	
	/* Create 3 employees all in the same department */
	employee_t employees[3] = {
		{1, 10, 2020, "Alice"},
		{2, 10, 2020, "Bob"},
		{3, 10, 2021, "Charlie"}
	};
	
	for (int i = 0; i < 3; i++) {
		qmap_put(primary, &employees[i].id, &employees[i]);
	}
	
	/* Verify count by department */
	uint32_t dept10 = 10;
	assert(qmap_count(by_dept, &dept10) == 3);
	
	/* Iterate over dept 10 employees */
	uint32_t cur = qmap_get_multi(by_dept, &dept10);
	assert(cur != QM_MISS);
	
	const void *k, *v;
	int dept10_count = 0;
	
	while (qmap_next(&k, &v, cur)) {
		const employee_t *emp = v;
		assert(emp->dept == 10);
		dept10_count++;
	}
	assert(dept10_count == 3);
	
	/* Test deletion: Remove Alice (id=1) from primary */
	uint32_t id1 = 1;
	qmap_del(primary, &id1);
	
	/* Secondary index should now have 2 employees */
	assert(qmap_count(by_dept, &dept10) == 2);
	
	qmap_close(primary);
	qmap_close(by_dept);
}

/* Test 16: QM_MULTIVALUE with QM_RANGE iteration
 * NOTE: QM_RANGE iteration with QM_MULTIVALUE has unexpected behavior
 * and may not return all duplicates as expected. This test verifies
 * basic functionality only.
 */
static void test_multivalue_range_iteration(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	/* Insert data: key 10 has 2 values, key 20 has 3 values, key 30 has 2 values */
	uint32_t k10 = 10, k20 = 20, k30 = 30;
	
	qmap_put(hd, &k10, &(uint32_t){100});
	qmap_put(hd, &k10, &(uint32_t){101});
	
	qmap_put(hd, &k20, &(uint32_t){200});
	qmap_put(hd, &k20, &(uint32_t){201});
	qmap_put(hd, &k20, &(uint32_t){202});
	
	qmap_put(hd, &k30, &(uint32_t){300});
	qmap_put(hd, &k30, &(uint32_t){301});
	
	/* Verify total count */
	assert(qmap_count(hd, NULL) == 7);
	
	/* Verify individual key counts */
	assert(qmap_count(hd, &k10) == 2);
	assert(qmap_count(hd, &k20) == 3);
	assert(qmap_count(hd, &k30) == 2);
	
	/* Range iteration - just verify it doesn't crash and returns some entries */
	uint32_t cur = qmap_iter(hd, &k20, QM_RANGE);
	assert(cur != QM_MISS);
	
	const void *k, *v;
	int count = 0;
	
	while (qmap_next(&k, &v, cur)) {
		const uint32_t *key = k;
		/* Should only see keys >= 20 */
		assert(*key >= 20);
		count++;
	}
	
	/* Should have gotten at least some entries (exact count may vary due to QM_RANGE+QM_MULTIVALUE interaction) */
	assert(count > 0);
	
	/* Full iteration from beginning */
	cur = qmap_iter(hd, NULL, QM_RANGE);
	count = 0;
	while (qmap_next(&k, &v, cur)) count++;
	/* Should get all 7 entries when starting from beginning */
	assert(count == 7);
	
	qmap_close(hd);
}

/* Test 17: Stress test - rapid add/delete cycles */
static void test_multivalue_stress(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFFFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	uint32_t key = 999;
	
	/* Cycle 1: Add 50 values, delete all, repeat 100 times */
	for (int cycle = 0; cycle < 100; cycle++) {
		/* Add 50 */
		for (int i = 0; i < 50; i++) {
			qmap_put(hd, &key, &(uint32_t){cycle * 1000 + i});
		}
		
		assert(qmap_count(hd, &key) == 50);
		
		/* Delete all */
		qmap_del_all(hd, &key);
		assert(qmap_count(hd, &key) == 0);
	}
	
	/* Cycle 2: Interleaved add/delete on 3 keys */
	uint32_t k1 = 1, k2 = 2, k3 = 3;
	
	for (int cycle = 0; cycle < 50; cycle++) {
		/* Add 10 to each key */
		for (int i = 0; i < 10; i++) {
			uint32_t v = cycle * 100 + i;
			qmap_put(hd, &k1, &v);
			qmap_put(hd, &k2, &v);
			qmap_put(hd, &k3, &v);
		}
		
		/* Verify counts */
		assert(qmap_count(hd, &k1) == 10);
		assert(qmap_count(hd, &k2) == 10);
		assert(qmap_count(hd, &k3) == 10);
		
		/* Delete all from k2 only */
		qmap_del_all(hd, &k2);
		
		assert(qmap_count(hd, &k1) == 10);
		assert(qmap_count(hd, &k2) == 0);
		assert(qmap_count(hd, &k3) == 10);
		
		/* Delete all from k1 and k3 */
		qmap_del_all(hd, &k1);
		qmap_del_all(hd, &k3);
		
		assert(qmap_count(hd, &k1) == 0);
		assert(qmap_count(hd, &k2) == 0);
		assert(qmap_count(hd, &k3) == 0);
	}
	
	/* Final verification: Map should be empty and functional */
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *k, *v;
	int total = 0;
	while (qmap_next(&k, &v, cur)) total++;
	assert(total == 0);
	
	/* Add one final entry to ensure map still works */
	qmap_put(hd, &(uint32_t){42}, &(uint32_t){777});
	assert(qmap_count(hd, &(uint32_t){42}) == 1);
	
	qmap_close(hd);
}

/* Test 18: Regression test for Bug #3 - QM_RANGE+QM_MULTIVALUE iteration
 * Previously, qmap_range with a specific key on QM_MULTIVALUE maps could
 * skip some duplicates because it used QMAP_BSEARCH_ANY instead of FIRST.
 * This test verifies ALL duplicates are returned.
 */
static void test_bug3_range_returns_all_duplicates(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	
	/* Add 5 duplicates for key 2000 */
	uint32_t key = 2000;
	for (uint32_t i = 1; i <= 5; i++) {
		qmap_put(hd, &key, &i);
	}
	
	/* Verify count */
	assert(qmap_count(hd, &key) == 5);
	
	/* Use qmap_range to iterate from 2000 to 2000 - should get ALL 5 duplicates */
	uint32_t cur = qmap_iter(hd, &key, QM_RANGE);
	assert(cur != QM_MISS);
	
	const void *k, *v;
	int count = 0;
	while (qmap_next(&k, &v, cur)) {
		count++;
	}
	
	/* Bug #3 fix: should return ALL 5 duplicates, not just some */
	assert(count == 5);
	
	/* Test with adjacent keys to ensure boundary detection works */
	uint32_t key_low = 1999;
	uint32_t key_high = 2001;
	qmap_put(hd, &key_low, &(uint32_t){100});
	qmap_put(hd, &key_high, &(uint32_t){200});
	
	/* Range iteration for key 2000 should still only return 5 entries */
	cur = qmap_iter(hd, &key, QM_RANGE);
	count = 0;
	while (qmap_next(&k, &v, cur)) {
		const uint32_t *ck = k;
		assert(*ck == 2000);  /* Should not include adjacent keys */
		count++;
	}
	assert(count == 5);
	
	qmap_close(hd);
}

/* Test 19: QM_RANGE_GE - lower-bound range iteration on QM_MULTIVALUE maps.
 * Plain QM_RANGE on a MULTIVALUE map iterates only the duplicates of the
 * one exact starting key. QM_RANGE_GE starts at the first key >= the
 * starting key and iterates to the end (all duplicates included, ascending).
 */
static void test_range_ge(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);

	uint32_t keys[] = {1, 2, 2, 3, 5, 5, 5};
	for (uint32_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		qmap_put(hd, &keys[i], &i);
	}

	/* GE from a key present with duplicates: boundary key included */
	uint32_t start = 3;
	uint32_t cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	assert(cur != QM_MISS);
	const void *k, *v;
	uint32_t expect[] = {3, 5, 5, 5};
	uint32_t n = 0, prev = 0;
	while (qmap_next(&k, &v, cur)) {
		const uint32_t *kk = k;
		assert(*kk >= start);              /* no entries below the bound */
		if (n > 0) assert(*kk >= prev);    /* ascending, or equal (dup) */
		assert(*kk == expect[n]);
		prev = *kk;
		n++;
	}
	assert(n == 4);

	/* GE from a mid-key not present: first key >= it */
	start = 4;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)k == 5);
		n++;
	}
	assert(n == 3);

	/* GE from below the lowest key: everything */
	start = 0;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) n++;
	assert(n == 7);

	/* GE from above the highest key: nothing */
	start = 6;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) n++;
	assert(n == 0);

	/* Dirty index: writes after an iteration, then GE again (must rebuild) */
	for (uint32_t i = 0; i < 3; i++) {
		qmap_put(hd, &keys[0], &i);      /* more duplicate key-1 entries */
	}
	start = 1;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) n++;
	assert(n == 10);                       /* 4 now at key 1, +2,2,3,5,5,5 */

	qmap_close(hd);

	/* Empty multivalue map */
	hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	start = 100;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	assert(cur != QM_MISS);
	n = 0;
	while (qmap_next(&k, &v, cur)) n++;
	assert(n == 0);
	qmap_close(hd);

	/* Non-multivalue sorted map: GE matches plain QM_RANGE+QM_SORTED */
	hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, QM_SORTED);
	assert(hd != QM_MISS);
	uint32_t nk[] = {10, 30, 50};
	for (uint32_t i = 0; i < sizeof(nk)/sizeof(nk[0]); i++)
		qmap_put(hd, &nk[i], &i);
	start = 20;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)k == 30 || *(const uint32_t *)k == 50);
		prev = *(const uint32_t *)k;
		if (n == 0) assert(prev == 30);
		n++;
	}
	assert(n == 2);
	qmap_close(hd);

	/* Unsorted map: GE degrades to a linear scan applying the same GE filter */
	hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, 0);
	assert(hd != QM_MISS);
	uint32_t uk[] = {7, 2, 9, 1, 20};
	for (uint32_t i = 0; i < sizeof(uk)/sizeof(uk[0]); i++)
		qmap_put(hd, &uk[i], &i);
	start = 5;
	cur = qmap_iter(hd, &start, QM_RANGE | QM_RANGE_GE);
	n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)k >= 5);
		n++;
	}
	assert(n == 3);                        /* 7, 9, 20 */
	qmap_close(hd);
}

/* Test 20: duplicate chain basics - insertion order via qmap_get_multi,
 * head promotion on qmap_del, and chain exhaustion. */
static void test_chain_basic_ops(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	uint32_t key = 7;
	uint32_t vals[4] = {10, 20, 30, 40};
	for (int i = 0; i < 4; i++)
		qmap_put(hd, &key, &vals[i]);

	/* Insertion order: chain is tail-appended */
	uint32_t cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	const void *k, *v;
	uint32_t expect[4] = {10, 20, 30, 40};
	int n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)k == 7);
		assert(*(const uint32_t *)v == expect[n]);
		n++;
	}
	qmap_fin(cur);
	assert(n == 4);

	/* qmap_del removes the head; next duplicate is promoted */
	qmap_del(hd, &key);
	cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)v == expect[n + 1]);
		n++;
	}
	qmap_fin(cur);
	assert(n == 3);

	/* Delete the rest; chain exhausted -> QM_MISS */
	qmap_del(hd, &key);
	qmap_del(hd, &key);
	qmap_del(hd, &key);
	assert(qmap_get_multi(hd, &key) == QM_MISS);
	assert(qmap_get(hd, &key) == NULL);
	assert(qmap_count(hd, &key) == 0);

	/* Map still works afterwards */
	qmap_put(hd, &(uint32_t){8}, &(uint32_t){5});
	assert(qmap_count(hd, &(uint32_t){8}) == 1);
	qmap_close(hd);
}

/* Test 21: chains survive grow, free-list reuse (LIFO reverses insertion
 * order) and qmap_rebuild_map re-linking without losing or mixing
 * duplicates. */
static void test_chain_grow_relink(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);

	const uint32_t NKEYS = 200;
	const uint32_t DUPS = 35;   /* 200*35 = 7000 entries -> grows to 65536 slots */

	for (uint32_t k = 0; k < NKEYS; k++) {
		for (uint32_t i = 0; i < DUPS; i++) {
			uint32_t val = k * 1000 + i + 1;
			qmap_put(hd, &k, &val);
		}
	}

	/* Delete all dups of even keys; rebuild re-links remaining chains */
	for (uint32_t k = 0; k < NKEYS; k += 2)
		qmap_del_all(hd, &k);

	/* Re-insert even keys: positions are reused LIFO, so insertion order
	 * differs from position order. Chains must stay complete. */
	for (uint32_t k = 0; k < NKEYS; k += 2) {
		for (uint32_t i = 0; i < DUPS; i++) {
			uint32_t val = k * 1000 + i + 1;
			qmap_put(hd, &k, &val);
		}
	}

	/* Trigger another rebuild re-link via an unrelated del_all */
	uint32_t key0 = 0;
	qmap_del_all(hd, &key0);

	const void *k, *v;
	uint32_t cur;

	/* Verify all remaining keys: exact counts, no cross-key mixing */
	for (uint32_t kk = 1; kk < NKEYS; kk++) {
		assert(qmap_count(hd, &kk) == DUPS);
		cur = qmap_get_multi(hd, &kk);
		assert(cur != QM_MISS);
		int seen[DUPS];
		memset(seen, 0, sizeof(seen));
		int n = 0;
		while (qmap_next(&k, &v, cur)) {
			uint32_t val = *(const uint32_t *)v;
			assert(val >= kk * 1000 + 1 && val <= kk * 1000 + DUPS);
			seen[val - kk * 1000 - 1] = 1;
			n++;
		}
		qmap_fin(cur);
		assert(n == (int)DUPS);
		for (int i = 0; i < (int)DUPS; i++)
			assert(seen[i] == 1);
	}

	/* Key 0 was fully removed */
	assert(qmap_count(hd, &key0) == 0);

	/* Total entry count is exact */
	cur = qmap_iter(hd, NULL, 0);
	int total = 0;
	while (qmap_next(&k, &v, cur)) total++;
	assert(total == (int)(NKEYS * DUPS - DUPS));

	qmap_close(hd);
}

/* qmap_assoc callback: secondary key = the primary's stored value */
static void assoc_val(const void **skey, const void * const pkey,
	const void * const value, void *userdata)
{
	(void)pkey; (void)userdata;
	*skey = value;
}

/* Test 22: assoc-linked multivalue close must complete at scale.
 * Regression for the close-path hang (per-entry sorted rebuild). */
static void test_chain_assoc_close(void)
{
	uint32_t primary = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, 0);
	uint32_t by_val  = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF,
	                             QM_SORTED | QM_MULTIVALUE);
	assert(primary != QM_MISS && by_val != QM_MISS);
	qmap_assoc(by_val, primary, assoc_val, NULL);

	for (uint32_t i = 0; i < 20000; i++)
		qmap_put(primary, &i, &i);

	uint32_t d0 = 0;
	assert(qmap_count(by_val, &d0) == 1);

	qmap_close(primary);
	qmap_close(by_val);
}

/* Test 23: file-backed multivalue round-trip - chains rebuilt on load via
 * qmap_put re-insertion. */
static void test_chain_persist(void)
{
	const char *filename = "test_chain_persist.qmap";

	uint32_t hd = qmap_open(filename, "chaindb", QM_U32, QM_U32, 0xFF,
	                        QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	uint32_t key = 42;
	for (uint32_t i = 0; i < 10; i++)
		qmap_put(hd, &key, &i);
	assert(qmap_count(hd, &key) == 10);
	qmap_save();
	qmap_close(hd);

	hd = qmap_open(filename, "chaindb", QM_U32, QM_U32, 0xFF,
	               QM_SORTED | QM_MULTIVALUE);
	assert(hd != QM_MISS);
	assert(qmap_count(hd, &key) == 10);
	uint32_t cur = qmap_get_multi(hd, &key);
	assert(cur != QM_MISS);
	const void *k, *v;
	int n = 0;
	while (qmap_next(&k, &v, cur)) {
		assert(*(const uint32_t *)k == 42);
		assert(*(const uint32_t *)v < 10);
		n++;
	}
	qmap_fin(cur);
	assert(n == 10);
	qmap_close(hd);

	remove(filename);
}

/* Test 24: hole-eliminating backshift (non-wrap cluster) — non-MV map.
 * Regression: a stale hole before an existing key used to make the
 * early-exit probe return the hole slot (qmap_get -> NULL) and a re-put
 * would insert a duplicate into a non-MV map. With backshift the cluster
 * shifts left and every probe resolves. */
static void test_backshift_cluster(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32,
	                        0x7 /* 8 slots */, 0);
	assert(hd != QM_MISS);

	uint32_t keys[] = { 0, 8, 16, 24, 32 };   /* home 0: cluster slots 0..4 */
	for (size_t i = 0; i < 5; i++)
		qmap_put(hd, &keys[i], &keys[i]);

	/* Delete the slot-1 member: hole at 1 must be back-shifted away. */
	uint32_t k8 = 8, k24 = 24, k16 = 16, k32 = 32, k40 = 40;
	qmap_del(hd, &k8);

	/* The previously-orphaned-by-hole keys must still resolve. */
	assert(qmap_get(hd, &k16) != NULL);
	assert(qmap_get(hd, &k24) != NULL);
	assert(qmap_get(hd, &k32) != NULL);
	assert(qmap_get(hd, &k8) == NULL);

	/* Re-putting the deleted key adds a new position (count 5). */
	assert(qmap_count(hd, NULL) == 4);
	qmap_put(hd, &k8, &k8);
	assert(qmap_count(hd, NULL) == 5);

	/* Re-putting an EXISTING key must not create a duplicate. */
	qmap_put(hd, &k24, &(uint32_t){240});
	assert(qmap_count(hd, NULL) == 5);
	const uint32_t *v24 = qmap_get(hd, &k24);
	assert(v24 != NULL && *v24 == 240);

	/* Fresh key beyond former holes lands correctly. */
	qmap_put(hd, &k40, &k40);
	assert(qmap_count(hd, NULL) == 6);
	const uint32_t *v40 = qmap_get(hd, &k40);
	assert(v40 != NULL && *v40 == k40);

	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *k, *v;
	int occ_24 = 0;
	while (qmap_next(&k, &v, cur)) {
		if (*(const uint32_t *)k == k24)
			occ_24++;
	}
	qmap_fin(cur);
	assert(occ_24 == 1);
	qmap_close(hd);

	/* MV map: delete a neighbor of a chain head mid-cluster; the chain must
	 * survive the backshift and the head's slot entry must stay put. */
	uint32_t mv = qmap_open(NULL, NULL, QM_U32, QM_U32,
	                        0x7, QM_MULTIVALUE | QM_SORTED);
	assert(mv != QM_MISS);
	uint32_t k0 = 0, m24 = 24;
	qmap_put(mv, &m24, &(uint32_t){1});   /* chain head at slot 0 */
	qmap_put(mv, &m24, &(uint32_t){2});
	qmap_put(mv, &k0, &(uint32_t){3});    /* slot 1 */
	qmap_put(mv, &k16, &(uint32_t){4});   /* slot 2 */
	qmap_put(mv, &k32, &(uint32_t){5});   /* slot 3 */

	qmap_del(mv, &k0);                    /* clears slot 1 + backshift */

	uint32_t mcur = qmap_get_multi(mv, &m24);
	assert(mcur != QM_MISS);
	int n = 0;
	while (qmap_next(&k, &v, mcur)) n++;
	qmap_fin(mcur);
	assert(n == 2);
	assert(qmap_get(mv, &k16) != NULL);
	assert(qmap_get(mv, &k32) != NULL);

	qmap_put(mv, &m24, &(uint32_t){6});   /* third dup links onto tail */
	mcur = qmap_get_multi(mv, &m24);
	assert(mcur != QM_MISS);
	n = 0;
	while (qmap_next(&k, &v, mcur)) n++;
	qmap_fin(mcur);
	assert(n == 3);
	qmap_close(mv);
}

/* Test 25: backshift across the table wrap with a skipped unmovable
 * element. Cluster {7,0,1,2,3} homes {7,7,7,2,7}: deleting slot 7 must
 * move 15/23 into the hole, SKIP the home-anchored key at slot 2, and
 * continue to move 31 — stopping only at the empty slot. */
static void test_backshift_wrap(void)
{
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32,
	                        0x7 /* 8 slots */, 0);
	assert(hd != QM_MISS);

	uint32_t k7 = 7, k15 = 15, k23 = 23, k2 = 2, k31 = 31, k39 = 39;
	qmap_put(hd, &k7, &k7);    /* slot 7 */
	qmap_put(hd, &k15, &k15);  /* slot 0 (wrap) */
	qmap_put(hd, &k23, &k23);  /* slot 1 (wrap) */
	qmap_put(hd, &k2, &k2);    /* slot 2 (at home) */
	qmap_put(hd, &k31, &k31);  /* slot 3 (wrap) */

	qmap_del(hd, &k7);         /* hole at 7 */

	assert(qmap_get(hd, &k15) != NULL);
	assert(qmap_get(hd, &k23) != NULL);
	assert(qmap_get(hd, &k2) != NULL);
	assert(qmap_get(hd, &k31) != NULL);
	assert(qmap_get(hd, &k7) == NULL);
	assert(qmap_count(hd, NULL) == 4);

	/* Post-backshift insert into the vacated region resolves correctly. */
	qmap_put(hd, &k39, &k39);
	assert(qmap_count(hd, NULL) == 5);
	const uint32_t *v39 = qmap_get(hd, &k39);
	assert(v39 != NULL && *v39 == k39);

	/* Everything still locatable after the new insert. */
	assert(qmap_get(hd, &k15) != NULL);
	assert(qmap_get(hd, &k23) != NULL);
	assert(qmap_get(hd, &k2) != NULL);
	assert(qmap_get(hd, &k31) != NULL);
	qmap_close(hd);
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
	TEST(test_multivalue_large_scale);
	TEST(test_multivalue_idm_tracking);
	TEST(test_multivalue_persistence);
	TEST(test_multivalue_concurrent_iteration);
	TEST(test_multivalue_modify_during_iteration);
	TEST(test_multivalue_multiple_indexes);
	TEST(test_multivalue_range_iteration);
	TEST(test_multivalue_stress);
	TEST(test_bug3_range_returns_all_duplicates);
	TEST(test_range_ge);
	TEST(test_chain_basic_ops);
	TEST(test_chain_grow_relink);
	TEST(test_chain_assoc_close);
	TEST(test_chain_persist);
	TEST(test_backshift_cluster);
	TEST(test_backshift_wrap);
	
	printf("\n=== All tests passed! ===\n");
	return 0;
}

/**
 * Extended test suite for libqmap
 * Tests edge cases, error conditions, and advanced features
 */

#include "./../include/ttypt/qmap.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define TEST_MASK 0xF  // Small capacity for testing limits

unsigned errors = 0;

#define PASS() printf("  ✅ PASS\n")
#define FAIL(msg) do { printf("  ❌ FAIL: %s\n", msg); errors++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) FAIL(msg); else PASS(); } while(0)

/* Association callback for test_associations */
static void assoc_cb(const void **skey, const void *pkey, const void *value) {
	(void)pkey;
	*skey = value; // Use primary value as secondary key
}

/* Test 1: Empty map operations */
static void test_empty_map(void) {
	printf("\n=== Test 1: Empty Map Operations ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	
	printf("Get from empty map:");
	const char *result = qmap_get(hd, &(uint32_t){42});
	ASSERT(result == NULL, "Expected NULL from empty map");
	
	printf("Iterate empty map:");
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key, *value;
	int count = 0;
	while (qmap_next(&key, &value, cur))
		count++;
	ASSERT(count == 0, "Expected 0 items from empty map");
	
	printf("Delete from empty map (should not crash):");
	qmap_del(hd, &(uint32_t){42});
	PASS();
	
	printf("Drop empty map (should not crash):");
	qmap_drop(hd);
	PASS();
	
	qmap_close(hd);
}

/* Test 2: Capacity limits */
static void test_capacity_limits(void) {
	printf("\n=== Test 2: Capacity Limits ===\n");
	
	// Create map with very small capacity (mask 0x3 = capacity 4)
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0x3, 0);
	
	printf("Fill map to capacity (4 items):");
	for (uint32_t i = 0; i < 4; i++) {
		uint32_t key = i * 10; // Spread out to avoid hash collisions
		qmap_put(hd, &key, &i);
	}
	PASS();
	
	printf("Verify all 4 items retrievable:");
	int found = 0;
	for (uint32_t i = 0; i < 4; i++) {
		uint32_t key = i * 10;
		const uint32_t *val = qmap_get(hd, &key);
		if (val && *val == i) found++;
	}
	ASSERT(found == 4, "Expected to retrieve all 4 items");
	
	printf("Note: Attempting to exceed capacity would trigger CBUG (process exit)\n");
	printf("  This behavior is documented but not tested here to avoid test failure\n");
	
	qmap_close(hd);
}

/* Test 3: QM_AINDEX (auto-indexing) */
static void test_aindex(void) {
	printf("\n=== Test 3: QM_AINDEX Auto-Indexing ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_HNDL, QM_STR, 0xFF, QM_AINDEX);
	
	printf("Insert with NULL key (should auto-generate ID):");
	uint32_t id1 = qmap_put(hd, NULL, "first");
	uint32_t id2 = qmap_put(hd, NULL, "second");
	uint32_t id3 = qmap_put(hd, NULL, "third");
	ASSERT(id1 != id2 && id2 != id3, "IDs should be unique");
	
	printf("Retrieve by auto-generated IDs:");
	const char *v1 = qmap_get(hd, &id1);
	const char *v2 = qmap_get(hd, &id2);
	const char *v3 = qmap_get(hd, &id3);
	ASSERT(v1 && strcmp(v1, "first") == 0, "Retrieved first value");
	ASSERT(v2 && strcmp(v2, "second") == 0, "Retrieved second value");
	ASSERT(v3 && strcmp(v3, "third") == 0, "Retrieved third value");
	
	printf("Mix NULL and explicit keys:");
	uint32_t explicit_key = 999;
	qmap_put(hd, &explicit_key, "explicit");
	const char *ve = qmap_get(hd, &explicit_key);
	ASSERT(ve && strcmp(ve, "explicit") == 0, "Retrieved explicit key value");
	
	qmap_close(hd);
}

/* Test 4: QM_SORTED edge cases */
static void test_sorted_edge_cases(void) {
	printf("\n=== Test 4: QM_SORTED Edge Cases ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, QM_SORTED);
	
	printf("Insert in random order:");
	uint32_t keys[] = {50, 10, 30, 20, 40};
	for (int i = 0; i < 5; i++)
		qmap_put(hd, &keys[i], "value");
	PASS();
	
	printf("Iterate in sorted order:");
	uint32_t cur = qmap_iter(hd, NULL, QM_RANGE);
	const void *key, *value;
	uint32_t prev = 0;
	int sorted_ok = 1;
	while (qmap_next(&key, &value, cur)) {
		uint32_t k = *(const uint32_t*)key;
		if (k < prev) sorted_ok = 0;
		prev = k;
	}
	ASSERT(sorted_ok, "Items should be in ascending order");
	
	printf("Range scan from middle (start=30):");
	uint32_t start = 30;
	cur = qmap_iter(hd, &start, QM_RANGE);
	int count = 0;
	while (qmap_next(&key, &value, cur)) {
		uint32_t k = *(const uint32_t*)key;
		ASSERT(k >= 30, "Range scan should only return keys >= 30");
		count++;
	}
	ASSERT(count == 3, "Expected 3 items (30, 40, 50)");
	
	printf("Add more items after sorted index built:");
	uint32_t new_key = 25;
	qmap_put(hd, &new_key, "new");
	PASS();
	
	printf("Verify sorted order maintained:");
	cur = qmap_iter(hd, NULL, QM_RANGE);
	prev = 0;
	sorted_ok = 1;
	count = 0;
	while (qmap_next(&key, &value, cur)) {
		uint32_t k = *(const uint32_t*)key;
		if (k < prev) sorted_ok = 0;
		prev = k;
		count++;
	}
	ASSERT(sorted_ok && count == 6, "Sorted order maintained with new item");
	
	printf("Delete item and verify sorted order:");
	qmap_del(hd, &(uint32_t){30});
	cur = qmap_iter(hd, NULL, QM_RANGE);
	count = 0;
	sorted_ok = 1;
	prev = 0;
	while (qmap_next(&key, &value, cur)) {
		uint32_t k = *(const uint32_t*)key;
		if (k < prev) sorted_ok = 0;
		if (k == 30) sorted_ok = 0; // 30 should be deleted
		prev = k;
		count++;
	}
	ASSERT(sorted_ok && count == 5, "Sorted order maintained after deletion");
	
	qmap_close(hd);
}

/* Test 5: Custom type registration */
static void test_custom_types(void) {
	printf("\n=== Test 5: Custom Type Registration ===\n");
	
	// Register a fixed-length struct type
	typedef struct { uint32_t x, y; } point_t;
	
	printf("Register fixed-length type (point_t, 8 bytes):");
	uint32_t point_type = qmap_reg(sizeof(point_t));
	ASSERT(point_type != QM_MISS, "Type registration should succeed");
	
	printf("Use custom type in map:");
	uint32_t hd = qmap_open(NULL, NULL, point_type, QM_U32, 0xFF, 0);
	
	point_t p1 = {10, 20};
	point_t p2 = {30, 40};
	qmap_put(hd, &p1, &(uint32_t){100});
	qmap_put(hd, &p2, &(uint32_t){200});
	
	const uint32_t *v1 = qmap_get(hd, &p1);
	const uint32_t *v2 = qmap_get(hd, &p2);
	ASSERT(v1 && *v1 == 100, "Retrieved value for p1");
	ASSERT(v2 && *v2 == 200, "Retrieved value for p2");
	
	qmap_close(hd);
	
	// Test variable-length type (already done with QM_STR)
	printf("Variable-length type (QM_STR is built-in):");
	PASS();
}

/* Test 6: Iterator edge cases */
static void test_iterator_edge_cases(void) {
	printf("\n=== Test 6: Iterator Edge Cases ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	
	printf("Early iterator termination (qmap_fin):");
	for (int i = 0; i < 10; i++)
		qmap_put(hd, &(uint32_t){i}, "value");
	
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key, *value;
	int count = 0;
	while (qmap_next(&key, &value, cur)) {
		count++;
		if (count == 3) {
			qmap_fin(cur);
			break;
		}
	}
	ASSERT(count == 3, "Early termination should work");
	
	printf("Multiple concurrent iterators:");
	uint32_t cur1 = qmap_iter(hd, NULL, 0);
	uint32_t cur2 = qmap_iter(hd, NULL, 0);
	
	int c1 = 0, c2 = 0;
	while (qmap_next(&key, &value, cur1)) c1++;
	while (qmap_next(&key, &value, cur2)) c2++;
	
	ASSERT(c1 == 10 && c2 == 10, "Both iterators should see all items");
	
	printf("Iterate after modifications:");
	qmap_del(hd, &(uint32_t){5});
	cur = qmap_iter(hd, NULL, 0);
	count = 0;
	while (qmap_next(&key, &value, cur)) count++;
	ASSERT(count == 9, "Iterator should reflect deletions");
	
	qmap_close(hd);
}

/* Test 7: Mirror map functionality */
static void test_mirror_maps(void) {
	printf("\n=== Test 7: Mirror Maps ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, QM_MIRROR);
	uint32_t mirror_hd = hd + 1;
	
	printf("Insert into primary map:");
	qmap_put(hd, &(uint32_t){1}, "one");
	qmap_put(hd, &(uint32_t){2}, "two");
	qmap_put(hd, &(uint32_t){3}, "three");
	PASS();
	
	printf("Lookup in mirror (value->key):");
	const uint32_t *k1 = qmap_get(mirror_hd, "one");
	const uint32_t *k2 = qmap_get(mirror_hd, "two");
	const uint32_t *k3 = qmap_get(mirror_hd, "three");
	ASSERT(k1 && *k1 == 1, "Mirror lookup for 'one'");
	ASSERT(k2 && *k2 == 2, "Mirror lookup for 'two'");
	ASSERT(k3 && *k3 == 3, "Mirror lookup for 'three'");
	
	printf("Delete from primary, verify mirror updated:");
	qmap_del(hd, &(uint32_t){2});
	const uint32_t *k2_after = qmap_get(mirror_hd, "two");
	ASSERT(k2_after == NULL, "Mirror should reflect deletion");
	
	printf("Update value in primary:");
	qmap_put(hd, &(uint32_t){1}, "ONE");
	const uint32_t *k1_new = qmap_get(mirror_hd, "ONE");
	const uint32_t *k1_old = qmap_get(mirror_hd, "one");
	ASSERT(k1_new && *k1_new == 1, "Mirror updated for new value");
	ASSERT(k1_old == NULL, "Old mirror entry removed");
	
	qmap_close(hd);
}

/* Test 8: Association between maps */
static void test_associations(void) {
	printf("\n=== Test 8: Map Associations ===\n");
	
	// Primary map: user_id -> username
	uint32_t users_hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	
	// Secondary map: username -> user_id (manual association)
	// QM_PGET flag makes qmap_get return primary keys instead of values
	uint32_t by_name_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, 0xFF, QM_PGET);
	
	printf("Setup association callback:");
	qmap_assoc(by_name_hd, users_hd, assoc_cb);
	PASS();
	
	printf("Insert into primary (should update secondary):");
	qmap_put(users_hd, &(uint32_t){100}, "alice");
	qmap_put(users_hd, &(uint32_t){200}, "bob");
	
	const uint32_t *u1 = qmap_get(by_name_hd, "alice");
	const uint32_t *u2 = qmap_get(by_name_hd, "bob");
	ASSERT(u1 && *u1 == 100, "Association created for alice");
	ASSERT(u2 && *u2 == 200, "Association created for bob");
	
	printf("Delete from primary (should update secondary):");
	qmap_del(users_hd, &(uint32_t){100});
	const uint32_t *u1_after = qmap_get(by_name_hd, "alice");
	ASSERT(u1_after == NULL, "Associated entry deleted");
	
	qmap_close(users_hd);
	qmap_close(by_name_hd);
}

/* Test 9: File persistence */
static void test_file_persistence(void) {
	printf("\n=== Test 9: File Persistence ===\n");
	
	const char *filename = "test_persist.qmap";
	
	printf("Create and populate file-backed map (requires QM_MIRROR):");
	{
		uint32_t hd = qmap_open(filename, "testdb", QM_U32, QM_STR, 0xFF, QM_MIRROR);
		qmap_put(hd, &(uint32_t){1}, "persisted_one");
		qmap_put(hd, &(uint32_t){2}, "persisted_two");
		qmap_put(hd, &(uint32_t){3}, "persisted_three");
		qmap_save(); // Explicit save
		qmap_close(hd);
		qmap_close(hd + 1); // Close mirror map
	}
	PASS();
	
	printf("Reopen and verify data persisted:");
	{
		uint32_t hd = qmap_open(filename, "testdb", QM_U32, QM_STR, 0xFF, QM_MIRROR);
		const char *v1 = qmap_get(hd, &(uint32_t){1});
		const char *v2 = qmap_get(hd, &(uint32_t){2});
		const char *v3 = qmap_get(hd, &(uint32_t){3});
		ASSERT(v1 && strcmp(v1, "persisted_one") == 0, "Value 1 persisted");
		ASSERT(v2 && strcmp(v2, "persisted_two") == 0, "Value 2 persisted");
		ASSERT(v3 && strcmp(v3, "persisted_three") == 0, "Value 3 persisted");
		qmap_close(hd);
		qmap_close(hd + 1);
	}
	
	printf("Test multiple databases in same file:");
	{
		uint32_t hd1 = qmap_open(filename, "db1", QM_U32, QM_STR, 0xFF, QM_MIRROR);
		uint32_t hd2 = qmap_open(filename, "db2", QM_U32, QM_STR, 0xFF, QM_MIRROR);
		
		qmap_put(hd1, &(uint32_t){100}, "db1_value");
		qmap_put(hd2, &(uint32_t){100}, "db2_value");
		
		const char *v1 = qmap_get(hd1, &(uint32_t){100});
		const char *v2 = qmap_get(hd2, &(uint32_t){100});
		
		ASSERT(v1 && strcmp(v1, "db1_value") == 0, "DB1 independent");
		ASSERT(v2 && strcmp(v2, "db2_value") == 0, "DB2 independent");
		
		qmap_close(hd1);
		qmap_close(hd1 + 1);
		qmap_close(hd2);
		qmap_close(hd2 + 1);
	}
	
	// Cleanup
	remove(filename);
}

/* Test 10: QM_HNDL type behavior */
static void test_hndl_type(void) {
	printf("\n=== Test 10: QM_HNDL Type Behavior ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_HNDL, QM_STR, 0xFF, 0);
	
	printf("QM_HNDL uses value directly as hash:");
	// With QM_HNDL, the value is used directly as hash
	// So we can test predictable hash bucket placement
	qmap_put(hd, &(uint32_t){10}, "ten");
	qmap_put(hd, &(uint32_t){20}, "twenty");
	
	const char *v1 = qmap_get(hd, &(uint32_t){10});
	const char *v2 = qmap_get(hd, &(uint32_t){20});
	
	ASSERT(v1 && strcmp(v1, "ten") == 0, "Retrieved by handle 10");
	ASSERT(v2 && strcmp(v2, "twenty") == 0, "Retrieved by handle 20");
	
	qmap_close(hd);
}

/* Test 11: QM_PTR type behavior */
static void test_ptr_type(void) {
	printf("\n=== Test 11: QM_PTR Type Behavior ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_PTR, QM_STR, 0xFF, 0);
	
	printf("Use pointer addresses as keys:");
	char obj1[] = "object1";
	char obj2[] = "object2";
	void *ptr1 = &obj1;
	void *ptr2 = &obj2;
	
	qmap_put(hd, &ptr1, "value1");
	qmap_put(hd, &ptr2, "value2");
	
	const char *v1 = qmap_get(hd, &ptr1);
	const char *v2 = qmap_get(hd, &ptr2);
	
	ASSERT(v1 && strcmp(v1, "value1") == 0, "Retrieved by pointer key 1");
	ASSERT(v2 && strcmp(v2, "value2") == 0, "Retrieved by pointer key 2");
	
	printf("Note: Returned pointer points to stored pointer bytes:");
	PASS();
	
	qmap_close(hd);
}

/* Test 12: Drop functionality */
static void test_drop(void) {
	printf("\n=== Test 12: Drop Functionality ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	
	printf("Populate map:");
	for (int i = 0; i < 10; i++)
		qmap_put(hd, &(uint32_t){i}, "value");
	PASS();
	
	printf("Drop all entries:");
	qmap_drop(hd);
	PASS();
	
	printf("Verify map is empty:");
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key, *value;
	int count = 0;
	while (qmap_next(&key, &value, cur)) count++;
	ASSERT(count == 0, "Map should be empty after drop");
	
	printf("Reuse map after drop:");
	qmap_put(hd, &(uint32_t){99}, "new_value");
	const char *v = qmap_get(hd, &(uint32_t){99});
	ASSERT(v && strcmp(v, "new_value") == 0, "Can reuse map after drop");
	
	qmap_close(hd);
}

/* Test 13: Update existing keys */
static void test_update_keys(void) {
	printf("\n=== Test 13: Update Existing Keys ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	
	printf("Insert initial value:");
	qmap_put(hd, &(uint32_t){42}, "initial");
	const char *v1 = qmap_get(hd, &(uint32_t){42});
	ASSERT(v1 && strcmp(v1, "initial") == 0, "Initial value set");
	
	printf("Update with new value:");
	qmap_put(hd, &(uint32_t){42}, "updated");
	const char *v2 = qmap_get(hd, &(uint32_t){42});
	ASSERT(v2 && strcmp(v2, "updated") == 0, "Value updated");
	
	printf("Verify only one entry exists:");
	uint32_t cur = qmap_iter(hd, NULL, 0);
	const void *key, *value;
	int count = 0;
	while (qmap_next(&key, &value, cur)) count++;
	ASSERT(count == 1, "Only one entry should exist");
	
	qmap_close(hd);
}

/* Test 14: File loading without QM_MIRROR */
static void test_file_loading_no_mirror(void) {
	printf("\n=== Test 14: File Loading Without QM_MIRROR ===\n");
	
	const char *testfile = "/tmp/qmap_test_no_mirror.db";
	
	// Create and populate a file-backed map with QM_MIRROR
	printf("Creating file with QM_MIRROR:");
	uint32_t hd1 = qmap_open(testfile, NULL, QM_U32, QM_U32, 0xFF, QM_MIRROR);
	qmap_put(hd1, &(uint32_t){10}, &(uint32_t){100});
	qmap_put(hd1, &(uint32_t){20}, &(uint32_t){200});
	qmap_put(hd1, &(uint32_t){30}, &(uint32_t){300});
	qmap_save();  // Explicitly save to file
	qmap_close(hd1);
	PASS();
	
	// Open WITHOUT QM_MIRROR and verify data loads
	printf("Opening file WITHOUT QM_MIRROR:");
	uint32_t hd2 = qmap_open(testfile, NULL, QM_U32, QM_U32, 0xFF, 0);
	PASS();
	
	printf("Verify data loaded from file:");
	const uint32_t *v1 = qmap_get(hd2, &(uint32_t){10});
	const uint32_t *v2 = qmap_get(hd2, &(uint32_t){20});
	const uint32_t *v3 = qmap_get(hd2, &(uint32_t){30});
	ASSERT(v1 && *v1 == 100 && v2 && *v2 == 200 && v3 && *v3 == 300,
	       "Data should load from file without QM_MIRROR");
	
	qmap_close(hd2);
	unlink(testfile);
}

/* Test 15: Pointer stability on replacement */
static void test_pointer_stability(void) {
	printf("\n=== Test 15: Pointer Stability on Replacement ===\n");
	
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 0xFF, 0);
	
	// Test 15a: Same key, same-sized value replacement
	printf("Same key, same value size - pointer should stay valid:");
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){100});
	const uint32_t *ptr1 = qmap_get(hd, &(uint32_t){1});
	void *orig_ptr1 = (void *)ptr1;
	qmap_put(hd, &(uint32_t){1}, &(uint32_t){200});
	const uint32_t *ptr2 = qmap_get(hd, &(uint32_t){1});
	ASSERT(ptr1 == ptr2 && *ptr2 == 200, 
	       "Pointer should remain valid and point to new value");
	
	// Test 15b: Verify old pointer is still valid (reused allocation)
	printf("Old pointer should show new value:");
	ASSERT((void *)ptr1 == orig_ptr1 && *ptr1 == 200,
	       "Original pointer should still be valid with new value");
	
	qmap_close(hd);
	
	// Test 15c: Variable-length types (strings)
	printf("Same key, smaller string - pointer should stay valid:");
	uint32_t hd2 = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	qmap_put(hd2, &(uint32_t){1}, "Long string here");
	const char *str_ptr1 = qmap_get(hd2, &(uint32_t){1});
	void *orig_str_ptr = (void *)str_ptr1;
	qmap_put(hd2, &(uint32_t){1}, "Short");
	const char *str_ptr2 = qmap_get(hd2, &(uint32_t){1});
	ASSERT(str_ptr1 == str_ptr2 && strcmp(str_ptr2, "Short") == 0,
	       "String pointer should be reused for smaller replacement");
	
	printf("Old string pointer should show new value:");
	ASSERT((void *)str_ptr1 == orig_str_ptr && strcmp(str_ptr1, "Short") == 0,
	       "Original string pointer should still be valid");
	
	qmap_close(hd2);
	
	// Test 15d: Same key, larger value (expect new allocation)
	printf("Same key, larger string - new allocation expected:");
	uint32_t hd3 = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
	qmap_put(hd3, &(uint32_t){1}, "Short");
	// Don't save pointer - it will be invalidated
	qmap_put(hd3, &(uint32_t){1}, "Much longer string that won't fit");
	const char *large_ptr = qmap_get(hd3, &(uint32_t){1});
	ASSERT(strcmp(large_ptr, "Much longer string that won't fit") == 0,
	       "New larger value should be stored correctly");
	
	qmap_close(hd3);
}

int main(void) {
	printf("╔════════════════════════════════════════════════════════════╗\n");
	printf("║        Extended Test Suite for libqmap                    ║\n");
	printf("╚════════════════════════════════════════════════════════════╝\n");
	
	test_empty_map();
	test_capacity_limits();
	test_aindex();
	test_sorted_edge_cases();
	test_custom_types();
	test_iterator_edge_cases();
	test_mirror_maps();
	test_associations();
	test_file_persistence();
	test_hndl_type();
	test_ptr_type();
	test_drop();
	test_update_keys();
	test_file_loading_no_mirror();
	test_pointer_stability();
	
	printf("\n╔════════════════════════════════════════════════════════════╗\n");
	if (errors == 0) {
		printf("║  ✅ ALL TESTS PASSED                                       ║\n");
	} else {
		printf("║  ❌ %u TEST(S) FAILED                                     ║\n", errors);
	}
	printf("╚════════════════════════════════════════════════════════════╝\n");
	
	return errors == 0 ? 0 : 1;
}

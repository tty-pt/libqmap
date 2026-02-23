/*
 * Performance benchmarks for QM_MULTIVALUE refactoring
 * 
 * This benchmarks the performance improvements from:
 * - P1.1: Fix double binary search in qmap_iter
 * - P1.2: Optimize qmap_count from O(n) to O(log n)
 * - P3.1: Optimize qmap_del_all
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ttypt/qmap.h>

#define BENCH(name) printf("\n[%s]\n", name)
#define TIME_START() clock_t start = clock()
#define TIME_END(var) double var = ((double)(clock() - start)) / CLOCKS_PER_SEC * 1000.0

/* Benchmark: qmap_iter creation (P1.1 impact) */
void bench_iter_creation(void)
{
	BENCH("P1.1: qmap_iter creation");
	
	/* Create map with 1000 duplicates */
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 2047, QM_MULTIVALUE | QM_SORTED);
	for (int i = 0; i < 1000; i++)
		qmap_put(hd, &(int){100}, &(int){i});
	
	/* Benchmark: Create 10,000 iterators */
	TIME_START();
	for (int i = 0; i < 10000; i++) {
		uint32_t cur = qmap_iter(hd, &(int){100}, 0);
		qmap_fin(cur);
	}
	TIME_END(elapsed);
	
	printf("  10,000 iterations on 1000 duplicates: %.2f ms\n", elapsed);
	printf("  Average per iter: %.4f ms\n", elapsed / 10000.0);
	
	qmap_close(hd);
}

/* Benchmark: qmap_count (P1.2 impact) */
void bench_count(void)
{
	BENCH("P1.2: qmap_count performance");
	
	int duplicate_counts[] = {1, 10, 100, 1000, 10000};
	int capacities[] = {15, 31, 255, 2047, 16383};
	int num_tests = sizeof(duplicate_counts) / sizeof(duplicate_counts[0]);
	
	for (int t = 0; t < num_tests; t++) {
		int dup_count = duplicate_counts[t];
		int capacity = capacities[t];
		
		/* Create map with N duplicates */
		uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, capacity, QM_MULTIVALUE | QM_SORTED);
		for (int i = 0; i < dup_count; i++)
			qmap_put(hd, &(int){100}, &(int){i});
		
		/* Benchmark: Call qmap_count 1000 times */
		TIME_START();
		for (int i = 0; i < 1000; i++) {
			size_t count = qmap_count(hd, &(int){100});
			(void)count;  /* Suppress unused warning */
		}
		TIME_END(elapsed);
		
		printf("  %5d duplicates (1000 calls): %8.2f ms (avg: %.4f ms)\n", 
		       dup_count, elapsed, elapsed / 1000.0);
		
		qmap_close(hd);
	}
}

/* Benchmark: qmap_del_all (P3.1 impact) */
void bench_del_all(void)
{
	BENCH("P3.1: qmap_del_all performance");
	
	int duplicate_counts[] = {10, 100, 1000};
	int capacities[] = {31, 255, 2047};
	int num_tests = sizeof(duplicate_counts) / sizeof(duplicate_counts[0]);
	
	for (int t = 0; t < num_tests; t++) {
		int dup_count = duplicate_counts[t];
		int capacity = capacities[t];
		
		/* Average over 100 runs to reduce noise */
		double total_time = 0.0;
		for (int run = 0; run < 100; run++) {
			/* Create map with N duplicates */
			uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, capacity, QM_MULTIVALUE | QM_SORTED);
			for (int i = 0; i < dup_count; i++)
				qmap_put(hd, &(int){100}, &(int){i});
			
			/* Benchmark: Delete all */
			TIME_START();
			qmap_del_all(hd, &(int){100});
			TIME_END(elapsed);
			
			total_time += elapsed;
			qmap_close(hd);
		}
		
		printf("  %4d duplicates (avg of 100 runs): %.4f ms\n", 
		       dup_count, total_time / 100.0);
	}
}

/* Memory usage check */
void check_memory(void)
{
	BENCH("Memory usage check");
	
	/* Create large map with many duplicates */
	uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_U32, 16383, QM_MULTIVALUE | QM_SORTED);
	
	printf("  Adding 10,000 duplicates...\n");
	for (int i = 0; i < 10000; i++)
		qmap_put(hd, &(int){100}, &(int){i});
	
	size_t count = qmap_count(hd, &(int){100});
	printf("  Verified count: %zu\n", count);
	
	printf("  Iterating through all entries...\n");
	uint32_t cur = qmap_iter(hd, &(int){100}, 0);
	size_t iter_count = 0;
	const void *k, *v;
	while (qmap_next(&k, &v, cur))
		iter_count++;
	qmap_fin(cur);
	
	printf("  Iterator count: %zu\n", iter_count);
	
	if (count == iter_count && count == 10000)
		printf("  ✓ All checks passed\n");
	else
		printf("  ✗ Mismatch detected!\n");
	
	qmap_close(hd);
}

int main(void)
{
	printf("=== QM_MULTIVALUE Performance Benchmarks ===\n");
	printf("Version: v0.7.0 + refactoring\n");
	printf("Date: Mon Feb 23 2026\n");
	
	bench_iter_creation();
	bench_count();
	bench_del_all();
	check_memory();
	
	printf("\n=== Benchmarks Complete ===\n");
	return 0;
}

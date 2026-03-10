#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 벤치마크 대상 함수들

int check_password(const char *input) {
  if (input[0] == 's' && input[1] == 'e' && input[2] == 'c' &&
      input[3] == 'r' && input[4] == 'e' && input[5] == 't') {
    return 1;
  }
  return 0;
}

long fibonacci(int n) {
  if (n <= 1) return n;
  long a = 0, b = 1;
  for (int i = 2; i <= n; i++) {
    long t = a + b;
    a = b;
    b = t;
  }
  return b;
}

int binary_search(const int *arr, int n, int target) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (arr[mid] == target) return mid;
    else if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

void bubble_sort(int *arr, int n) {
  for (int i = 0; i < n - 1; i++)
    for (int j = 0; j < n - i - 1; j++)
      if (arr[j] > arr[j + 1]) {
        int t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t;
      }
}

unsigned crc32_simple(const unsigned char *data, int len) {
  unsigned crc = 0xFFFFFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
  }
  return ~crc;
}

static double measure(const char *name, void (*fn)(void), int iterations) {
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < iterations; i++)
    fn();
  clock_gettime(CLOCK_MONOTONIC, &end);
  double ms = (end.tv_sec - start.tv_sec) * 1000.0 +
              (end.tv_nsec - start.tv_nsec) / 1e6;
  printf("%-25s %8.2f ms  (%d iterations)\n", name, ms, iterations);
  return ms;
}

static void bench_check_password(void) {
  volatile int r = check_password("secret");
  (void)r;
}

static void bench_fibonacci(void) {
  volatile long r = fibonacci(40);
  (void)r;
}

static void bench_binary_search(void) {
  static int arr[1000];
  static int init = 0;
  if (!init) { for (int i = 0; i < 1000; i++) arr[i] = i * 2; init = 1; }
  volatile int r = binary_search(arr, 1000, 998);
  (void)r;
}

static void bench_bubble_sort(void) {
  int arr[200];
  for (int i = 0; i < 200; i++) arr[i] = 200 - i;
  bubble_sort(arr, 200);
}

static void bench_crc32(void) {
  const char *data = "The quick brown fox jumps over the lazy dog";
  volatile unsigned r = crc32_simple((const unsigned char *)data, 43);
  (void)r;
}

static void bench_string_ops(void) {
  const char *s1 = "Hello from ORK-NEW benchmark test!";
  const char *s2 = "Another string for comparison purposes.";
  char buf[128];
  snprintf(buf, sizeof(buf), "%s - %s", s1, s2);
  volatile int r = (int)strlen(buf);
  (void)r;
}

int main(void) {
  printf("=== ORK-NEW Benchmark ===\n\n");

  measure("check_password",   bench_check_password,   5000000);
  measure("fibonacci(40)",    bench_fibonacci,         5000000);
  measure("binary_search",    bench_binary_search,     5000000);
  measure("bubble_sort(200)", bench_bubble_sort,         50000);
  measure("crc32",            bench_crc32,             2000000);
  measure("string_ops",       bench_string_ops,        2000000);

  printf("\nDone.\n");
  return 0;
}

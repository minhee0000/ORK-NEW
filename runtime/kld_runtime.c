#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>

// === 인플레이스 복호화 + 스레드 안전 캐시 ===
#define KLD_CACHE_SIZE 1024

typedef struct {
  const uint8_t *data;  // 복호화 완료된 데이터 주소 (원본 위치)
} kld_cache_entry;

static kld_cache_entry kld_cache[KLD_CACHE_SIZE];
static int kld_cache_count = 0;
static pthread_mutex_t kld_mutex = PTHREAD_MUTEX_INITIALIZER;

// === 후킹/리프팅 공격 방지 ===
typedef void *(*kld_decrypt_fn)(uint8_t *, uint8_t, size_t);

static void *kld_decrypt_impl(uint8_t *data, uint8_t key, size_t len);

// 함수 포인터 테이블 (코드 리프팅 방지)
static volatile kld_decrypt_fn kld_fn_table[] = {
    kld_decrypt_impl,
    kld_decrypt_impl, // redundancy
};

static void *kld_decrypt_impl(uint8_t *data, uint8_t key, size_t len) {
  pthread_mutex_lock(&kld_mutex);

  // 캐시 검색: 이미 복호화된 데이터인지 확인
  for (int i = 0; i < kld_cache_count; i++) {
    if (kld_cache[i].data == data) {
      pthread_mutex_unlock(&kld_mutex);
      return data;  // 이미 인플레이스 복호화 완료
    }
  }

  // 인플레이스 복호화 (원본 데이터를 직접 수정)
  for (size_t i = 0; i < len; i++) {
    uint8_t k = key ^ (uint8_t)(i & 0xFF);
    data[i] ^= k;
  }

  // 캐시 등록 (복호화 완료 표시)
  if (kld_cache_count < KLD_CACHE_SIZE) {
    kld_cache[kld_cache_count].data = data;
    __sync_synchronize(); // 메모리 배리어
    kld_cache_count++;
  }

  pthread_mutex_unlock(&kld_mutex);
  return data;
}

// 외부 진입점: 간접 호출로 후킹 방지
void *__kld_decrypt_lazy(uint8_t *data, uint8_t key, size_t len) {
  volatile kld_decrypt_fn fn = kld_fn_table[0];
  return fn(data, key, len);
}

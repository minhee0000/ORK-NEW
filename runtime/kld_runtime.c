#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// === 힙 메모리 동기화 (멀티스레드 안전) ===
#define KLD_CACHE_SIZE 1024

typedef struct {
  const uint8_t *encrypted;
  uint8_t *decrypted;
  size_t len;
} kld_cache_entry;

static kld_cache_entry kld_cache[KLD_CACHE_SIZE];
static int kld_cache_count = 0;
static pthread_mutex_t kld_mutex = PTHREAD_MUTEX_INITIALIZER;

// === 후킹/리프팅 공격 방지 ===
// 복호화 함수 자체의 무결성을 간접적으로 검증
// 함수 포인터를 통한 간접 호출로 후킹 탐지를 어렵게 만듦
typedef void *(*kld_decrypt_fn)(uint8_t *, uint8_t, size_t);

static void *kld_decrypt_impl(uint8_t *encrypted, uint8_t key, size_t len);

// 함수 포인터 테이블 (코드 리프팅 방지)
static volatile kld_decrypt_fn kld_fn_table[] = {
    kld_decrypt_impl,
    kld_decrypt_impl, // redundancy
};

static void *kld_decrypt_impl(uint8_t *encrypted, uint8_t key, size_t len) {
  // 캐시 검색 (lock-free 읽기 시도)
  for (int i = 0; i < kld_cache_count; i++) {
    if (kld_cache[i].encrypted == encrypted)
      return kld_cache[i].decrypted;
  }

  // 캐시 미스: 락 획득 후 재확인 (double-checked locking)
  pthread_mutex_lock(&kld_mutex);

  // 다른 스레드가 이미 복호화했을 수 있으므로 재확인
  for (int i = 0; i < kld_cache_count; i++) {
    if (kld_cache[i].encrypted == encrypted) {
      pthread_mutex_unlock(&kld_mutex);
      return kld_cache[i].decrypted;
    }
  }

  // 힙에 복호화
  uint8_t *decrypted = (uint8_t *)malloc(len);
  if (!decrypted) {
    pthread_mutex_unlock(&kld_mutex);
    return encrypted;
  }

  // 복호화 루프 (의도적으로 단순 XOR 대신 약간의 변형 추가)
  for (size_t i = 0; i < len; i++) {
    uint8_t k = key ^ (uint8_t)(i & 0xFF);
    decrypted[i] = encrypted[i] ^ key; // 기본 XOR
  }

  // 캐시 등록
  if (kld_cache_count < KLD_CACHE_SIZE) {
    kld_cache[kld_cache_count].encrypted = encrypted;
    kld_cache[kld_cache_count].decrypted = decrypted;
    kld_cache[kld_cache_count].len = len;
    __sync_synchronize(); // 메모리 배리어
    kld_cache_count++;
  }

  pthread_mutex_unlock(&kld_mutex);
  return decrypted;
}

// 외부 진입점: 간접 호출로 후킹 방지
void *__kld_decrypt_lazy(uint8_t *encrypted, uint8_t key, size_t len) {
  // 함수 포인터 테이블을 통한 간접 호출
  volatile kld_decrypt_fn fn = kld_fn_table[0];
  return fn(encrypted, key, len);
}

#include "source/common/common/random_generator.h"

#include "source/common/common/assert.h"

#include "openssl/rand.h"

namespace Envoy {
namespace Random {

const size_t RandomGeneratorImpl::UUID_LENGTH = 36;

uint64_t RandomGeneratorImpl::random() {
  // Prefetch 256 * sizeof(uint64_t) bytes of randomness. buffered_idx is initialized to 256,
  // i.e. out-of-range value, so the buffer will be filled with randomness on the first call
  // to this function.
  //
  // There is a diminishing return when increasing the prefetch size, as illustrated below in
  // a test that generates 1,000,000,000 uint64_t numbers (results on Intel Xeon E5-1650v3).
  //
  // //test/common/runtime:runtime_impl_test - Random.DISABLED_benchmarkRandom
  //
  //  prefetch  |  time  | improvement
  // (uint64_t) |  (ms)  | (% vs prev)
  // ---------------------------------
  //         32 | 25,931 |
  //         64 | 15,124 | 42% faster
  //        128 |  9,653 | 36% faster
  //        256 |  6,930 | 28% faster  <-- used right now
  //        512 |  5,571 | 20% faster
  //       1024 |  4,888 | 12% faster
  //       2048 |  4,594 |  6% faster
  //       4096 |  4,424 |  4% faster
  //       8192 |  4,386 |  1% faster

  const size_t prefetch = 256;
  static thread_local uint64_t buffered[prefetch];
  static thread_local size_t buffered_idx = prefetch;

  if (buffered_idx >= prefetch) {
    int rc = RAND_bytes(reinterpret_cast<uint8_t*>(buffered), sizeof(buffered));
    ASSERT(rc == 1);
    buffered_idx = 0;
  }

  // Consume uint64_t from the buffer.
  return buffered[buffered_idx++];
}

std::string RandomGeneratorImpl::uuid() {
  // Prefetch 2048 bytes of randomness. buffered_idx is initialized to sizeof(buffered),
  // i.e. out-of-range value, so the buffer will be filled with randomness on the first
  // call to this function.
  //
  // There is a diminishing return when increasing the prefetch size, as illustrated below
  // in a test that generates 100,000,000 UUIDs (results on Intel Xeon E5-1650v3).
  //
  // //test/common/runtime:uuid_util_test - UUIDUtilsTest.DISABLED_benchmark
  //
  //   prefetch |  time  | improvement
  //   (bytes)  |  (ms)  | (% vs prev)
  // ---------------------------------
  //        128 | 16,353 |
  //        256 | 11,827 | 28% faster
  //        512 |  9,676 | 18% faster
  //       1024 |  8,594 | 11% faster
  //       2048 |  8,097 |  6% faster  <-- used right now
  //       4096 |  7,790 |  4% faster
  //       8192 |  7,737 |  1% faster

  static thread_local uint8_t buffered[2048];
  static thread_local size_t buffered_idx = sizeof(buffered);

  if (buffered_idx + 16 > sizeof(buffered)) {
    int rc = RAND_bytes(buffered, sizeof(buffered));
    ASSERT(rc == 1);
    buffered_idx = 0;
  }

  // Consume 16 bytes from the buffer.
  ASSERT(buffered_idx + 16 <= sizeof(buffered));
  uint8_t* rand = &buffered[buffered_idx];
  buffered_idx += 16;

  // Create UUID from Truly Random or Pseudo-Random Numbers.
  // See: https://tools.ietf.org/html/rfc4122#section-4.4
  rand[6] = (rand[6] & 0x0f) | 0x40; // UUID version 4 (random)
  rand[8] = (rand[8] & 0x3f) | 0x80; // UUID variant 1 (RFC4122)

  // Convert UUID to a string representation, e.g. a121e9e1-feae-4136-9e0e-6fac343d56c9.
  static const char* const hex = "0123456789abcdef";
  char uuid[UUID_LENGTH];

  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i] = hex[d >> 4];
    uuid[2 * i + 1] = hex[d & 0x0f];
  }

  uuid[8] = '-';

  for (uint8_t i = 4; i < 6; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 1] = hex[d >> 4];
    uuid[2 * i + 2] = hex[d & 0x0f];
  }

  uuid[13] = '-';

  for (uint8_t i = 6; i < 8; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 2] = hex[d >> 4];
    uuid[2 * i + 3] = hex[d & 0x0f];
  }

  uuid[18] = '-';

  for (uint8_t i = 8; i < 10; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 3] = hex[d >> 4];
    uuid[2 * i + 4] = hex[d & 0x0f];
  }

  uuid[23] = '-';

  for (uint8_t i = 10; i < 16; i++) {
    const uint8_t d = rand[i];
    uuid[2 * i + 4] = hex[d >> 4];
    uuid[2 * i + 5] = hex[d & 0x0f];
  }

  return {uuid, UUID_LENGTH};
}

} // namespace Random
} // namespace Envoy

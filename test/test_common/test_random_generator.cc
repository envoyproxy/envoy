#include "test/test_common/test_random_generator.h"

#include "gtest/gtest.h"

using testing::GTEST_FLAG(random_seed);

namespace Envoy {

// The purpose of using the static seed here is to use --test_arg=--gtest_random_seed=[seed]
// to specify the seed of the problem to replay.
int32_t getSeed() {
  static const int32_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  return seed;
}

TestRandomGenerator::TestRandomGenerator()
    : seed_(GTEST_FLAG(random_seed) == 0 ? getSeed() : GTEST_FLAG(random_seed)), generator_(seed_) {
  ENVOY_LOG_MISC(info, "TestRandomGenerator running with seed {}", seed_);
}

uint64_t TestRandomGenerator::random() { return generator_(); }

} // namespace Envoy

#pragma once

#include <random>

#include "source/common/common/utility.h"

namespace Envoy {

// Random number generator which logs its seed to stderr. To repeat a test run with a non-zero seed
// one can run the test with --test_arg=--gtest_random_seed=[seed]
class TestRandomGenerator {
public:
  TestRandomGenerator();

  uint64_t random();

private:
  const int32_t seed_;
  std::ranlux48 generator_;
};

} // namespace Envoy

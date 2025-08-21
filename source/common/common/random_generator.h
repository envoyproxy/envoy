#pragma once

#include "envoy/common/random_generator.h"

namespace Envoy {
namespace Random {

/**
 * Utility class for generating random numbers and UUIDs. These methods in class are thread-safe.
 * NOTE: The RandomGenerator from FactoryContext should be used as priority. Only use this class
 * when RandomGenerator is not available and unit test mocking is not needed.
 */
class RandomUtility {
public:
  static uint64_t random();
  static std::string uuid();
};

/**
 * Implementation of RandomGenerator that uses per-thread RANLUX generators seeded with current
 * time.
 */
class RandomGeneratorImpl : public RandomGenerator {
public:
  // Random::RandomGenerator
  uint64_t random() override;
  std::string uuid() override;

  static const size_t UUID_LENGTH;
};

} // namespace Random
} // namespace Envoy

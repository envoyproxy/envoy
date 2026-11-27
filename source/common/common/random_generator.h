#pragma once

#include <array>
#include <cstdint>
#include <string>

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
  // Binary form of a UUID, as specified in RFC 4122.
  using UuidBytes = std::array<uint8_t, 16>;

  static uint64_t random();
  static std::string uuid();

  /**
   * @return a version 4 (random) UUID in the binary form. 122 bits are drawn from the
   * cryptographically secure random number generator of the SSL library.
   */
  static UuidBytes uuidBytes();

  /**
   * @return the canonical string form of the UUID, e.g. a121e9e1-feae-4136-9e0e-6fac343d56c9.
   */
  static std::string uuidToString(const UuidBytes& uuid);
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

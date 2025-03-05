#pragma once

#include "source/extensions/hash/algorithm_provider.h"

namespace Envoy {
namespace Extensions {
namespace Hash {

/**
 * Algorithm provider is an interface for calculating hash.
 */
class SHA1AlgorithmImpl : public AlgorithmProvider {
public:
  /**
   * Calculates hash for a provided input.
   * @return std::string containing the hash.
   */
  std::string computeHash(absl::string_view input) override;

  /**
   * Returns algorithm-specific length of the calculated hash.
   */
  uint32_t digestLength() override;
};

using SHA1AlgorithmImplSharedPtr = std::shared_ptr<SHA1AlgorithmImpl>;

} // namespace Hash
} // namespace Extensions
} // namespace Envoy

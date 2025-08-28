#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Hash {

/**
 * Algorithm provider is an interface for calculating hash.
 */
class AlgorithmProvider {
public:
  virtual ~AlgorithmProvider() = default;

  /**
   * Calculates hash for a provided input.
   * @return std::string containing the hash.
   */
  virtual std::string computeHash(absl::string_view input) PURE;

  /**
   * Returns algorithm-specific length of the calculated hash.
   */
  virtual uint32_t digestLength() PURE;

  /**
   * Returns algorithm-specific length of a hash encoded in base64.
   */
  virtual uint32_t base64EncodedHashLength() PURE;
};

using AlgorithmProviderSharedPtr = std::shared_ptr<AlgorithmProvider>;

} // namespace Hash
} // namespace Extensions
} // namespace Envoy

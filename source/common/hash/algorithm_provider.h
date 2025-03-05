#pragma once

namespace Envoy {
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
  std::string computeHash(absl::string_view input) PURE;
};

using AlgorithmProviderSharedPtr = std::shared_ptr<AlgorithmProvider>;

} // namespace Hash
} // namespace Envoy

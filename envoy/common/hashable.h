#pragma once

#include <cstdint>
#include <optional>

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Interface for hashable types used in heterogeneous contexts (see, for example, usage in
 * FilterStateHashMethod).
 */
class Hashable {
public:
  virtual ~Hashable() = default;

  /**
   * Request the 64-bit hash for this object.
   * @return std::optional<uint64_t> the hash value, or std::nullopt if a hash could not be
   * produced for this instance.
   */
  virtual std::optional<uint64_t> hash() const PURE;
};

} // namespace Envoy

#pragma once

#include <cstdint>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

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
   * @return absl::optional<uint64_t> the hash value, or absl::nullopt if a hash could not be
   * produced for this instance.
   */
  virtual absl::optional<uint64_t> hash() const PURE;
};

} // namespace Envoy

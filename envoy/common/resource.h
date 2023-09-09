#pragma once

#include <cstdint>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

#pragma once

namespace Envoy {

/**
 * A handle for use by any resource managers.
 */
class ResourceLimit {
public:
  virtual ~ResourceLimit() = default;

  /**
   * @return true if the resource can be created.
   */
  virtual bool canCreate() PURE;

  /**
   * Increment the resource count.
   */
  virtual void inc() PURE;

  /**
   * Decrement the resource count.
   */
  virtual void dec() PURE;

  /**
   * Decrement the resource count by a specific amount.
   */
  virtual void decBy(uint64_t amount) PURE;

  /**
   * @return the current maximum allowed number of this resource.
   */
  virtual uint64_t max() PURE;

  /**
   * @return the current resource count.
   */
  virtual uint64_t count() const PURE;
};

using ResourceLimitOptRef = absl::optional<std::reference_wrapper<ResourceLimit>>;

} // namespace Envoy

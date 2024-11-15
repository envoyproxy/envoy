#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"
#include "library/common/api/c_types.h"

namespace Envoy {
namespace Platform {

/**
 * `StringAccessor` is an interface that may be implemented to provide access to an arbitrary
 * string from the application.
 */
class StringAccessor : public std::enable_shared_from_this<StringAccessor> {
public:
  virtual ~StringAccessor() = default;

  /**
   * Returns the string associated with this accessor.
   */
  virtual const std::string& get() const PURE;

  /**
   * Maps an implementation to its internal representation.
   * @return portable internal type used to call an implementation.
   */
  static envoy_string_accessor asEnvoyStringAccessor(std::shared_ptr<StringAccessor> accessor);
};

using StringAccessorSharedPtr = std::shared_ptr<StringAccessor>;

} // namespace Platform
} // namespace Envoy

#pragma once

#include "envoy/common/pure.h"
#include "envoy/request_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Contains a string in a form which is usable with FilterState and
 * allows lazy evaluation if needed. All values meant to be accessible to the
 * custom request/response header mechanism must use this type.
 */
class StringAccessor : public RequestInfo::FilterState::Object {
public:
  /**
   * @return the string the accessor represents.
   */
  virtual absl::string_view asString() const PURE;
};

} // namespace Router
} // namespace Envoy

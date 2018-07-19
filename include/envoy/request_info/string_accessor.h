#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class StringAccessor {
 public:
  virtual ~StringAccessor() PURE;
  absl::string_view asString() const PURE;
};

} // namespace RequestInfo
} // namespace Envoy

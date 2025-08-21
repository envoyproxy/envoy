#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Runtime {

// This class provides a place to register runtime keys which must be accessed by
// generic classes.
class Keys {
public:
  static const absl::string_view GlobalMaxCxRuntimeKey;
};

} // namespace Runtime
} // namespace Envoy

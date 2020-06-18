#include <memory>

#include "absl/memory/memory.h"

namespace Envoy {

auto to_be_fix = absl::make_unique<int>(0);

// Awesome stuff goes here.

} // namespace Envoy

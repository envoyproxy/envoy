#include <memory>

#include "absl/memory/memory.h"

namespace Envoy {

std::unique_ptr<int> to_be_fix = absl::make_unique<int>(0);

// Awesome stuff goes here.

} // namespace Envoy

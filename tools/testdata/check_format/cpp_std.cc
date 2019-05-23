#include <memory>

#include "absl/memory/memory.h"

namespace Envoy {

std::unique_ptr<int> bad = absl::make_unique<int>(0);	
std::unique_ptr<int> good = std::make_unique<int>(0);	

// Awesome stuff goes here.

} // namespace Envoy

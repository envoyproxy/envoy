#pragma once

#include <string>

#include "envoy/common/random_generator.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace WebSocket {

std::string generateKey(Random::RandomGenerator& random);

std::string computeAccept(absl::string_view key);

} // namespace WebSocket
} // namespace Envoy

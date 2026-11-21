#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace WebSocket {

std::string generateKey();

std::string computeAccept(absl::string_view key);

} // namespace WebSocket
} // namespace Envoy

#pragma once

#include <exception>
#include <memory>
#include <optional>
#include <string>

#include "absl/types/optional.h"

namespace Envoy {
namespace Platform {

struct EnvoyError {
  int error_code;
  std::string message;
  absl::optional<int> attempt_count;
  absl::optional<std::exception> cause;
};

using EnvoyErrorSharedPtr = std::shared_ptr<EnvoyError>;

} // namespace Platform
} // namespace Envoy

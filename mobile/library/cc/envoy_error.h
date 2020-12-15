#pragma once

// NOLINT(namespace-envoy)

#include <exception>
#include <memory>
#include <optional>
#include <string>

#include "absl/types/optional.h"

struct EnvoyError {
  int error_code;
  std::string message;
  absl::optional<int> attempt_count;
  std::exception cause;
};

using EnvoyErrorSharedPtr = std::shared_ptr<EnvoyError>;

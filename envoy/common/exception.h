#pragma once

#include <stdexcept>
#include <string>

namespace Envoy {
/**
 * Base class for all envoy exceptions.
 */
class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};

#define THROW_IF_NOT_OK(status_fn)                                                                 \
  {                                                                                                \
    absl::Status status = status_fn;                                                               \
    if (!status.ok()) {                                                                            \
      throw EnvoyException(std::string(status.message()));                                         \
    }                                                                                              \
  }

// Simple macro to handle bridging functions which return absl::StatusOr, and
// functions which throw errors.
//
// The completely unnecessary throw action argument is just so 'throw' appears
// at the call site, so format checks about use of exceptions are triggered.
#define THROW_IF_STATUS_NOT_OK(variable, throw_action)                                             \
  if (!variable.status().ok()) {                                                                   \
    throw_action EnvoyException(std::string(variable.status().message()));                         \
  }

#define RETURN_IF_STATUS_NOT_OK(variable)                                                          \
  if (!variable.status().ok()) {                                                                   \
    return variable.status();                                                                      \
  }
} // namespace Envoy

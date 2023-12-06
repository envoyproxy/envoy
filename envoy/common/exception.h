#pragma once

#include <stdexcept>
#include <string>

#include "source/common/common/assert.h"

namespace Envoy {

// This is a workaround to allow an exceptionless Envoy Mobile build while we
// have not finished plumbing Satus/StatusOr<> based error handling, so
// hard-failing instead. See
// (https://github.com/envoyproxy/envoy-mobile/issues/176)
// for example error handling PRs.
// TODO(alyssawilk) finish up error handling and remove this.
#ifdef ENVOY_DISABLE_EXCEPTIONS
#define throwEnvoyExceptionOrPanic(x) PANIC(x)
#define throwExceptionOrPanic(x, y) PANIC(y)
#else
#define throwEnvoyExceptionOrPanic(x) throw EnvoyException(x)
#define throwExceptionOrPanic(y, x) throw y(x)
#endif

/**
 * Base class for all envoy exceptions.
 */
class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};

#define THROW_IF_NOT_OK_REF(status)                                                                \
  do {                                                                                             \
    if (!(status).ok()) {                                                                          \
      throwEnvoyExceptionOrPanic(std::string((status).message()));                                 \
    }                                                                                              \
  } while (0)

#define THROW_IF_NOT_OK(status_fn)                                                                 \
  do {                                                                                             \
    const absl::Status status = (status_fn);                                                       \
    THROW_IF_NOT_OK_REF(status);                                                                   \
  } while (0)

// Simple macro to handle bridging functions which return absl::StatusOr, and
// functions which throw errors.
//
// The completely unnecessary throw_action argument was just so 'throw' appears
// at the call site, so format checks about use of exceptions would be triggered.
// This didn't work, so the variable is no longer used and is not duplicated in
// the macros above.
#define THROW_IF_STATUS_NOT_OK(variable, throw_action) THROW_IF_NOT_OK_REF(variable.status());

#define RETURN_IF_STATUS_NOT_OK(variable)                                                          \
  if (!variable.status().ok()) {                                                                   \
    return variable.status();                                                                      \
  }
} // namespace Envoy

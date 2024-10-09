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
#define throwEnvoyExceptionOrPanic(x) throw ::Envoy::EnvoyException(x)
#define throwExceptionOrPanic(y, x) throw y(x)
#endif

/**
 * Base class for all envoy exceptions.
 */
class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};

#define SET_AND_RETURN_IF_NOT_OK(check_status, set_status)                                         \
  if (const absl::Status temp_status = check_status; !temp_status.ok()) {                          \
    set_status = temp_status;                                                                      \
    return;                                                                                        \
  }

#define THROW_IF_NOT_OK_REF(status)                                                                \
  do {                                                                                             \
    if (!(status).ok()) {                                                                          \
      throwEnvoyExceptionOrPanic(std::string((status).message()));                                 \
    }                                                                                              \
  } while (0)

// Simple macro to handle bridging functions which return absl::StatusOr, and
// functions which throw errors.
#define THROW_IF_NOT_OK(status_fn)                                                                 \
  do {                                                                                             \
    const absl::Status status = (status_fn);                                                       \
    THROW_IF_NOT_OK_REF(status);                                                                   \
  } while (0)

#define RETURN_IF_NOT_OK_REF(variable)                                                             \
  if (const absl::Status& temp_status = variable; !temp_status.ok()) {                             \
    return temp_status;                                                                            \
  }

// Make sure this works for functions without calling the functoin twice as well.
#define RETURN_IF_NOT_OK(status_fn)                                                                \
  if (absl::Status temp_status = (status_fn); !temp_status.ok()) {                                 \
    return temp_status;                                                                            \
  }

template <class Type> Type returnOrThrow(absl::StatusOr<Type> type_or_error) {
  THROW_IF_NOT_OK_REF(type_or_error.status());
  return std::move(type_or_error.value());
}

#define THROW_OR_RETURN_VALUE(expression, type) returnOrThrow<type>(expression)

} // namespace Envoy

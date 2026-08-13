#pragma once

// NOLINT(namespace-envoy)

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define COROUTINE_STATUS_MACROS_IMPL_CONCAT_INNER(x, y) x##y
#define COROUTINE_STATUS_MACROS_IMPL_CONCAT(x, y) COROUTINE_STATUS_MACROS_IMPL_CONCAT_INNER(x, y)

/**
 * Evaluates `expr` (producing `absl::Status`) and `co_return`s early if `!status.ok()`.
 *
 * Example:
 *   CO_RETURN_IF_ERROR(co_await buffer.flush());
 */
#define CO_RETURN_IF_ERROR_IMPL(status, ...)                                                       \
  do {                                                                                             \
    auto status = (__VA_ARGS__);                                                                   \
    if (!status.ok()) {                                                                            \
      co_return std::move(status);                                                                 \
    }                                                                                              \
  } while (false)

#define CO_RETURN_IF_ERROR(...)                                                                    \
  CO_RETURN_IF_ERROR_IMPL(COROUTINE_STATUS_MACROS_IMPL_CONCAT(_coro_status_, __COUNTER__),         \
                          __VA_ARGS__)

/**
 * Evaluates `rexpr` (producing `absl::StatusOr<T>`), `co_return`s early if `!statusor.ok()`,
 * or unpacks the value into `lhs`.
 *
 * Examples:
 *   ASSIGN_OR_CO_RETURN(auto res, co_await client.fetch());
 *   ASSIGN_OR_CO_RETURN(chunk, co_await buffer.recv());
 */
#define ASSIGN_OR_CO_RETURN_IMPL(statusor, lhs, ...)                                               \
  auto statusor = (__VA_ARGS__);                                                                   \
  if (!statusor.ok()) {                                                                            \
    co_return std::move(statusor).status();                                                        \
  }                                                                                                \
  lhs = std::move(statusor).value()

#define ASSIGN_OR_CO_RETURN(lhs, ...)                                                              \
  ASSIGN_OR_CO_RETURN_IMPL(COROUTINE_STATUS_MACROS_IMPL_CONCAT(_coro_status_or_, __COUNTER__),     \
                           lhs, __VA_ARGS__)

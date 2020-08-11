#pragma once

#include <atomic>
#include <string>

#include "envoy/http/codes.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

/**
 * Facility for returning rich error information.
 * This facility is to be used in place of exceptions, in components where
 * exceptions safety is not guaranteed (i.e. codecs).
 *
 * Envoy::Status is an alias of absl::Status.
 * IMPORTANT: `absl::Status` constructor `absl::Status::code()` and absl::Status::ToString()`
 * methods must not be used as they will not return correct error information. Instead the error
 * value creating and corresponding error checking functions defined below must be used.
 * TODO(yanavlasov): add clang-tidy or lint check to enforce this.
 *
 * Usage example:
 *
 *  Envoy::Status Foo() {
 *    ...
 *    if (codec_error) {
 *      return CodecProtocolError("Invalid protocol");
 *    }
 *    return Envoy::OkStatus();
 *  }
 *
 *  void Bar() {
 *    auto status = Foo();
 *    if (status.ok()) {
 *      ...
 *    } else {
 *      ASSERT(IsCodecProtocolError(status));
 *      ENVOY_LOG(debug, "Codec error encountered: {}", status.message());
 *    }
 *  }
 */

namespace Envoy {
namespace Http {

/**
 * Status codes for representing classes of Envoy errors.
 */
enum class StatusCode : int {
  Ok = 0,

  /**
   * Indicates a non-recoverable protocol error that should result in connection termination.
   */
  CodecProtocolError = 1,

  /**
   * Indicates detection of outbound frame queue flood.
   */
  BufferFloodError = 2,

  /**
   * Indicates a response is received on a connection that did not send a request. In practice
   * this can only happen on HTTP/1.1 connections.
   */
  PrematureResponseError = 3,

  /**
   * Indicates a client (local) side error which should not happen.
   */
  CodecClientError = 4
};

using Status = absl::Status;

inline Status okStatus() { return absl::OkStatus(); }

/**
 * Returns the combination of the error code name, message and any additional error attributes.
 */
std::string toString(const Status& status);

/**
 * Functions for creating error values. The error code of the returned status object matches the
 * name of the function.
 */
Status codecProtocolError(absl::string_view message);
Status bufferFloodError(absl::string_view message);
Status prematureResponseError(absl::string_view message, Http::Code http_code);
Status codecClientError(absl::string_view message);

/**
 * Returns Envoy::StatusCode of the given status object.
 * If the status object does not contain valid Envoy::Status value the function will ASSERT.
 */
StatusCode getStatusCode(const Status& status);

/**
 * Returns true if the given status matches error code implied by the name of the function.
 */
ABSL_MUST_USE_RESULT bool isCodecProtocolError(const Status& status);
ABSL_MUST_USE_RESULT bool isBufferFloodError(const Status& status);
ABSL_MUST_USE_RESULT bool isPrematureResponseError(const Status& status);
ABSL_MUST_USE_RESULT bool isCodecClientError(const Status& status);

/**
 * Returns Http::Code value of the PrematureResponseError status.
 * IsPrematureResponseError(status) must be true which is checked by ASSERT.
 */
Http::Code getPrematureResponseHttpCode(const Status& status);

/**
 * Macro that checks return value of expression that results in Status and returns from
 * the current function is status is not OK.
 *
 * Example usage:
 *   Status foo() {
 *     RETURN_IF_ERROR(bar());
 *     return okStatus();
 *   }
 */

#define RETURN_IF_ERROR(expr)                                                                      \
  do {                                                                                             \
    if (::Envoy::Http::Details::StatusAdapter adapter{(expr)}) {                                   \
    } else {                                                                                       \
      return std::move(adapter.status_);                                                           \
    }                                                                                              \
  } while (false)

namespace Details {
// Helper class to convert `Status` to `bool` so it can be used inside `if` statements.
struct StatusAdapter {
  StatusAdapter(const Status& status) : status_(status) {}
  StatusAdapter(Status&& status) : status_(std::move(status)) {}

  StatusAdapter(const StatusAdapter&) = delete;
  StatusAdapter& operator=(const StatusAdapter&) = delete;

  explicit operator bool() const { return status_.ok(); }

  Status status_;
};
} // namespace Details

} // namespace Http
} // namespace Envoy

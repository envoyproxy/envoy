#pragma once

#include <atomic>
#include <string>

#include "envoy/http/codes.h"

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

/**
 * Status codes for representing classes of codec errors.
 */
enum class StatusCode : int {
  Ok = 0,
  CodecProtocolError = 1,
  BufferFloodError = 2,
  PrematureResponse = 3,
  CodecClientError = 4
};

/**
 * Class for returning error status.
 * This class is to replace the use of exceptions in Envoy's codecs.
 * Requirements:
 *   1. Represent the type system of existing codec exceptions.
 *   2. Highly efficient when the status has NO error.
 *   3. Register size, to avoid stack allocations.
 *   4. Extensible.
 *
 * Constructing an error status is likely as heavy as throwing an exception due to a heap
 * allocation.
 */
class ABSL_MUST_USE_RESULT Status final {
public:
  // Creates an OK status with no message.
  Status() = default;

  /**
   * Create a status with the specified code and
   * error message. If `code == StatusCode::Ok`, `msg` is ignored and an
   * object identical to an OK status is constructed.
   */
  Status(StatusCode code, absl::string_view msg);

  /**
   * Create a status with the specified code, HTTP code and
   * error message. If `code == StatusCode::Ok`, `msg` and `http_code` are ignored and an
   * object identical to an OK status is constructed.
   */
  Status(StatusCode code, Http::Code http_code, absl::string_view msg);

  Status(const Status& other) : rep_(other.rep_) { Ref(); }
  Status& operator=(const Status& x);

  // The moved-from state is the Ok.
  Status(Status&& other) noexcept : rep_(other.rep_) { other.rep_ = nullptr; }
  Status& operator=(Status&&) noexcept;

  ~Status();

  // Returns true if the Status is OK.
  ABSL_MUST_USE_RESULT bool Ok() const { return rep_ == nullptr; }

  // Returns the error code.
  StatusCode Code() const { return Ok() ? StatusCode::Ok : rep_->status_code_; }

  /**
   * Returns the error message. Note: prefer ToString() for debug logging.
   * This message rarely describes the error code. It is not unusual for the
   * error message to be the empty string.
   */
  absl::string_view Message() const { return Ok() ? *EmptyString() : rep_->message_; }

  // Returns HTTP code if it was provided during Status construction
  absl::optional<Envoy::Http::Code> HttpCode() const {
    return Ok() ? absl::nullopt : rep_->http_code_;
  }

  /**
   * Returns a combination of the error code name, HTTP status if it was specified and the message.
   * You can expect the code name and the message to be substrings of the
   * result.
   * WARNING: Do not depend on the exact format of the result of `ToString()`
   * which is subject to change.
   */
  std::string ToString() const { return Ok() ? "OK" : ToStringError(); }

  /**
   * Ignores any errors. This method does nothing except potentially suppress
   * complaints from any tools that are checking that errors are not dropped on
   * the floor.
   */
  void IgnoreError() const {}

private:
  static const std::string* EmptyString();

  // Returns string for non-ok Status.
  std::string ToStringError() const;

  struct StatusRep {
    StatusRep(StatusCode code, absl::string_view msg)
        : ref_(0), status_code_(code), message_(msg) {}
    StatusRep(StatusCode code, Http::Code http_code, absl::string_view msg)
        : ref_(0), status_code_(code), message_(msg), http_code_(http_code) {}

    std::atomic<int32_t> ref_;
    StatusCode status_code_;
    std::string message_;
    absl::optional<Envoy::Http::Code> http_code_;
  };

  void Ref() {
    if (rep_) {
      rep_->ref_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  void Unref();

  StatusRep* rep_{nullptr};
};

inline Status& Status::operator=(const Status& other) { // NOLINT self assignment
  if (other.rep_ != rep_) {
    Unref();
    rep_ = other.rep_;
    Ref();
  }
  return *this;
}

inline Status& Status::operator=(Status&& other) noexcept {
  Unref();
  rep_ = other.rep_;
  Ref();
  return *this;
}

// Helper to make code returning Ok status more readable.
inline Status OkStatus() { return Status(); }

} // namespace Http
} // namespace Envoy

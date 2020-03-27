#pragma once

#include "common/http/status.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Http {

/**
 * Union of the Status and a T object.
 * An object of StatusOr<T> has either usable value or an error
 * explaining why the value is not present.
 * This class is used to facilitate replacement of exceptions with
 * a return code in HTTP codecs. For instance method Foo() that throws an exception and returns an
 * int
 *
 *  int Foo() {
 *    ...
 *    if (error_condition) {
 *      throw CodecProtocolException("Error");
 *    }
 *    return valid_value;
 *  }
 *
 * can be replaced with:
 *
 *  StatusOr<int> Foo() {
 *    ...
 *    if (error_condition) {
 *      return Status(StatusCode::CodecProtocolError, "Error");
 *    }
 *    return valid_value;
 *  }
 */
template <typename T> class StatusOr {
  using StatusOrValue = absl::variant<class Status, T>;

public:
  /**
   * Construct a new StatusOr with the given non-ok status. After calling
   * this constructor, calls to Value() will RELEASE_ASSERT-fail.
   *
   * NOTE: Not explicit - we want to use StatusOr<T> as a return
   * value, so it is convenient and sensible to be able to do 'return
   * Status(...)' when the return type is StatusOr<T>.
   *
   * REQUIRES: status != StatusCode::Ok(). This requirement is checked with RELEASE_ASSERT.
   */
  StatusOr(const Status& status) : status_or_value_(status) {
    RELEASE_ASSERT(!status.Ok(), "Status must not be Ok");
  }

  /**
   * Construct a new StatusOr with the given value.
   * After calling this constructor, calls to
   * Value() will succeed, and calls to Status() will return OK.
   *
   * NOTE: Not explicit - we want to use StatusOr<T> as a return type
   * so it is convenient and sensible to be able to do 'return T()'
   * when when the return type is StatusOr<T>.
   */
  StatusOr(const T& value) : status_or_value_(value) {}
  StatusOr(T&& value) : status_or_value_(std::move(value)) {}

  /**
   * Conversion copy constructor and assignment operator, T must be copy constructible from U
   * TODO(yanavlasov): disable this constructor for cases where T can be copy constructed from
   * StatusOr<U> to avoid ambiguity.
   */
  template <typename U>
  StatusOr(const StatusOr<U>& other)
      : status_or_value_(other.Ok() ? StatusOrValue(other.Value())
                                    : StatusOrValue(other.Status())) {}
  template <typename U> StatusOr& operator=(const StatusOr<U>& other) {
    status_or_value_ = other.Ok() ? StatusOrValue(other.Value()) : StatusOrValue(other.Status());
    return *this;
  }

  /**
   * Move constructor is a bit weird. Move operation must leave the moved out of object in a valid
   * state. However moved out of Status is in the Ok() state, which makes the StatusOr to be in an
   * invalid state, there status is Ok but there is no value. As such move operations move the value
   * BUT copy the status, such that the moved out of StatusOr remains valid when it carries Status.
   * It is a bit inefficient for error cases, but still allows T to be a move only type
   */
  StatusOr(StatusOr&& other) noexcept
      : status_or_value_(other.Ok() ? StatusOrValue(std::move(other).Value())
                                    : StatusOrValue(other.Status())) {}
  StatusOr& operator=(StatusOr&& other) noexcept {
    status_or_value_ =
        other.Ok() ? StatusOrValue(std::move(other).Value()) : StatusOrValue(other.Status());
    return *this;
  }
  StatusOr(const StatusOr&) = default;
  StatusOr& operator=(const StatusOr&) = default;

  template <typename U>
  StatusOr(StatusOr<U>&& other)
      : status_or_value_(other.Ok() ? StatusOrValue(std::move(other).Value())
                                    : StatusOrValue(other.Status())) {}
  template <typename U> StatusOr& operator=(StatusOr<U>&& other) {
    status_or_value_ =
        other.Ok() ? StatusOrValue(std::move(other).Value()) : StatusOrValue(other.Status());
    return *this;
  }

  /**
   * Returns a reference to our status. If this contains a T, then
   * returns Status::OK.
   */
  const Status& Status() const {
    static const class Status ok_status;
    return Ok() ? ok_status : absl::get<class Status>(status_or_value_);
  }

  bool Ok() const { return absl::holds_alternative<T>(status_or_value_); }

  // Returns various kinds of references to the current value, or RELEASE_ASSERT if !this->ok().
  const T& Value() const& {
    RELEASE_ASSERT(Ok(), "Status must be Ok");
    return absl::get<T>(status_or_value_);
  }
  T& Value() & {
    RELEASE_ASSERT(Ok(), "Status must be Ok");
    return absl::get<T>(status_or_value_);
  }

  // Support move operation on underlying value. Invoke using `std::move(status_or).Value()`
  T&& Value() && {
    RELEASE_ASSERT(Ok(), "Status must be Ok");
    return absl::get<T>(std::move(status_or_value_));
  }
  const T&& Value() const&& {
    RELEASE_ASSERT(Ok(), "Status must be Ok");
    return absl::get<T>(std::move(status_or_value_));
  }

private:
  StatusOrValue status_or_value_;
};

} // namespace Http
} // namespace Envoy

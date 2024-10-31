#pragma once

#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace CancelWrapper {

using CancelFunction = absl::AnyInvocable<void()>;

// Wraps a callback with a cancellation function. The cancellation function is saved
// in an 'out' pointer argument to facilitate simple chaining in place, e.g. typical
// usage would be of the form
//
// someFunctionThatTakesACallback(cancelWrapped([this](SomeResponse response) {
//   doStuff(response);
// }, &cancel_callback_));
//
// Then cancel_callback_ could be called (conditionally on having been populated) in the
// class destructor to prevent use of "this" in a dispatched event that ends up occurring
// after the calling object was destroyed.
//
// This cancellation is not safe to be called between threads - it is intended to be used in
// conjunction with dispatchers for events that will be occurring on a single thread but
// whose order may be unpredictable due to outside triggers.
template <typename Callback> auto cancelWrapped(Callback&& callback, CancelFunction* cancel_out) {
  auto cancelled_flag = std::make_shared<bool>(false);
  *cancel_out = [cancelled_flag]() { *cancelled_flag = true; };
  return [cb = std::move(callback),
          cancelled_flag = std::move(cancelled_flag)](auto&&... args) mutable {
    if (*cancelled_flag) {
      return;
    }
    return std::move(cb)(std::forward<decltype(args)>(args)...);
  };
}

} // namespace CancelWrapper
} // namespace Envoy

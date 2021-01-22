#pragma once

#include <chrono>
#include <ostream>

#include "common/common/interval_value.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Event {

/**
 * Describes a minimum timer value that is equal to a scale factor applied to the maximum.
 */
struct ScaledMinimum {
  explicit constexpr ScaledMinimum(UnitFloat scale_factor) : scale_factor_(scale_factor) {}
  inline bool operator==(const ScaledMinimum& other) const {
    return other.scale_factor_.value() == scale_factor_.value();
  }
  const UnitFloat scale_factor_;
};

/**
 * Describes a minimum timer value that is an absolute duration.
 */
struct AbsoluteMinimum {
  explicit constexpr AbsoluteMinimum(std::chrono::milliseconds value) : value_(value) {}
  inline bool operator==(const AbsoluteMinimum& other) const { return other.value_ == value_; }
  const std::chrono::milliseconds value_;
};

/**
 * Class that describes how to compute a minimum timeout given a maximum timeout value. It wraps
 * ScaledMinimum and AbsoluteMinimum and provides a single computeMinimum() method.
 */
class ScaledTimerMinimum {
public:
  // Forward arguments to impl_'s constructor.
  constexpr ScaledTimerMinimum(AbsoluteMinimum arg) : impl_(arg) {}
  constexpr ScaledTimerMinimum(ScaledMinimum arg) : impl_(arg) {}

  // Computes the minimum value for a given maximum timeout. If this object was constructed with a
  // - ScaledMinimum value:
  //     the return value is the scale factor applied to the provided maximum.
  // - AbsoluteMinimum:
  //     the return value is that minimum, and the provided maximum is ignored.
  std::chrono::milliseconds computeMinimum(std::chrono::milliseconds maximum) const {
    struct Visitor {
      explicit Visitor(std::chrono::milliseconds value) : value_(value) {}
      std::chrono::milliseconds operator()(ScaledMinimum scale_factor) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            scale_factor.scale_factor_.value() * value_);
      }
      std::chrono::milliseconds operator()(AbsoluteMinimum absolute_value) {
        return absolute_value.value_;
      }
      const std::chrono::milliseconds value_;
    };
    return absl::visit(Visitor(maximum), impl_);
  }

  inline bool operator==(const ScaledTimerMinimum& other) const { return impl_ == other.impl_; }

private:
  absl::variant<ScaledMinimum, AbsoluteMinimum> impl_;
};

enum class ScaledTimerType {
  // Timers created with this type will never be scaled. This should only be used for testing.
  UnscaledRealTimerForTest,
  // The amount of time an HTTP connection to a downstream client can remain idle (no streams). This
  // corresponds to the HTTP_DOWNSTREAM_CONNECTION_IDLE TimerType in overload.proto.
  HttpDownstreamIdleConnectionTimeout,
  // The amount of time an HTTP stream from a downstream client can remain idle. This corresponds to
  // the HTTP_DOWNSTREAM_STREAM_IDLE TimerType in overload.proto.
  HttpDownstreamIdleStreamTimeout,
  // The amount of time a connection to a downstream client can spend waiting for the transport to
  // report connection establishment before the connection is closed. This corresponds to the
  // TRANSPORT_SOCKET_CONNECT_TIMEOUT TimerType in overload.proto.
  TransportSocketConnectTimeout,
};

using ScaledTimerTypeMap = absl::flat_hash_map<ScaledTimerType, ScaledTimerMinimum>;
using ScaledTimerTypeMapConstSharedPtr = std::shared_ptr<const ScaledTimerTypeMap>;

} // namespace Event
} // namespace Envoy

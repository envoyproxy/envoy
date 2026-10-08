#include "source/common/network/drain_close_util.h"

#include <chrono>
#include <cstdint>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

envoy::config::listener::v3::Listener::DrainType listenerDrainType(const Connection& connection) {
  const OptRef<const ListenerInfo> listener_info =
      connection.connectionInfoProvider().listenerInfo();
  if (!listener_info.has_value()) {
    return envoy::config::listener::v3::Listener::DEFAULT;
  }
  return listener_info->drainType();
}

bool shouldDrainClose(Server::Configuration::ServerFactoryContext& context,
                      envoy::config::listener::v3::Listener::DrainType drain_type,
                      std::optional<ConnectionDrainEvent> drain_event, double ramp_factor) {
  // If we are actively health check failed and the drain type is default, always drain close. This
  // is polled rather than driven by a drain notification because /healthcheck/ok reverses it.
  //
  // TODO(mattklein123): In relation to x-envoy-immediate-health-check-fail, it would be better
  // if even in the case of server health check failure we had some period of drain ramp up.
  if (drain_type == envoy::config::listener::v3::Listener::DEFAULT && context.healthCheckFailed()) {
    return true;
  }

  // The connection has not been notified of a drain sequence, so there is nothing to ramp against.
  if (!drain_event.has_value()) {
    return false;
  }

  // An immediate strategy drains as soon as the connection has been notified.
  if (drain_event->strategy == Server::DrainStrategy::Immediate) {
    return true;
  }
  ASSERT(drain_event->strategy == Server::DrainStrategy::Gradual);

  // Gradual strategy: P(return true) = elapsed time / drain time, matching
  // Server::DrainManagerImpl::drainClose(). The drain start time was captured once on the main
  // thread so every connection shares the same drain deadline.
  // The ramp may be compressed into the first part of the drain window, see ramp_factor. Rounded
  // up so that a short drain time keeps its (whole seconds) ramp instead of collapsing to zero.
  ASSERT(!(ramp_factor < 0.0 || ramp_factor > 1.0));
  const std::chrono::seconds drain_time =
      std::chrono::ceil<std::chrono::seconds>(context.options().drainTime() * ramp_factor);
  const MonotonicTime now = context.timeSource().monotonicTime();
  // Guard against a clock reading earlier than the recorded start time (should not happen with a
  // monotonic clock, but be defensive).
  const auto elapsed =
      now <= drain_event->start_time
          ? std::chrono::seconds{0}
          : std::chrono::duration_cast<std::chrono::seconds>(now - drain_event->start_time);
  // Also covers a zero drain time, in which case we drain as soon as we are notified.
  if (elapsed >= drain_time) {
    return true;
  }
  return static_cast<uint64_t>(elapsed.count()) >
         (context.api().randomGenerator().random() % static_cast<uint64_t>(drain_time.count()));
}

std::chrono::milliseconds proactiveDrainDelay(Server::Configuration::ServerFactoryContext& context,
                                              ConnectionDrainEvent drain_event,
                                              std::chrono::milliseconds grace_period,
                                              double window_start_factor) {
  ASSERT(!(window_start_factor < 0.0 || window_start_factor > 1.0));
  const std::chrono::milliseconds drain_time = context.options().drainTime();
  const MonotonicTime now = context.timeSource().monotonicTime();
  const auto elapsed =
      now <= drain_event.start_time
          ? std::chrono::milliseconds{0}
          : std::chrono::duration_cast<std::chrono::milliseconds>(now - drain_event.start_time);

  // The caller needs grace_period to shut down gracefully, so the window ends that long before the
  // drain deadline. A drain time too short to leave any window means there is nothing to spread the
  // connections over: draining them all at once would be the very burst this is meant to avoid.
  const auto window_start =
      std::chrono::duration_cast<std::chrono::milliseconds>(drain_time * window_start_factor);
  const auto window_end = drain_time - grace_period;
  if (window_start >= window_end) {
    return std::chrono::milliseconds{0};
  }

  // A connection notified once the window has opened was accepted during the drain. Leave it
  // alone: its first response drains it anyway, as the ramp is over by then.
  if (elapsed >= window_start) {
    return std::chrono::milliseconds{0};
  }

  // Both bounds are strictly positive here, so the delay is too.
  const uint64_t span = (window_end - window_start).count();
  const std::chrono::milliseconds jitter{context.api().randomGenerator().random() % span};
  return window_start + jitter - elapsed;
}

} // namespace Network
} // namespace Envoy

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
                      std::optional<ConnectionDrainEvent> drain_event) {
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
  const std::chrono::seconds drain_time = context.options().drainTime();
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

} // namespace Network
} // namespace Envoy

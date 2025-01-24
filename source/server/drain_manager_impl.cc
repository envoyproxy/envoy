#include "source/server/drain_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/timer.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Server {

DrainManagerImpl::DrainManagerImpl(Instance& server,
                                   envoy::config::listener::v3::Listener::DrainType drain_type)
    : server_(server), drain_type_(drain_type) {}

bool DrainManagerImpl::drainClose(Network::DrainDirection direction) const {
  // If we are actively health check failed and the drain type is default, always drain close.
  //
  // TODO(mattklein123): In relation to x-envoy-immediate-health-check-fail, it would be better
  // if even in the case of server health check failure we had some period of drain ramp up. This
  // would allow the other side to fail health check for the host which will require some thread
  // jumps versus immediately start GOAWAY/connection thrashing.
  if (drain_type_ == envoy::config::listener::v3::Listener::DEFAULT &&
      server_.healthCheckFailed()) {
    return true;
  }

  if (!draining_.load().first) {
    return false;
  }

  // If the direction passed in is greater than the current draining direction
  // (e.g. direction = ALL, but draining_.second == INBOUND_ONLY), then don't
  // drain. We also don't want to drain if the direction is None (which doesn't really
  // make sense, but it's the correct behavior).
  if (direction == Network::DrainDirection::None || direction > draining_.load().second) {
    return false;
  }

  if (server_.options().drainStrategy() == Server::DrainStrategy::Immediate) {
    return true;
  }
  ASSERT(server_.options().drainStrategy() == Server::DrainStrategy::Gradual);

  // P(return true) = elapsed time / drain timeout
  // If the drain deadline is exceeded, skip the probability calculation.
  const MonotonicTime current_time = server_.dispatcher().timeSource().monotonicTime();
  auto deadline = drain_deadlines_.find(direction);
  ASSERT(deadline != drain_deadlines_.end());
  if (current_time >= deadline->second) {
    return true;
  }

  const auto remaining_time =
      std::chrono::duration_cast<std::chrono::seconds>(deadline->second - current_time);
  const auto drain_time = server_.options().drainTime();
  ASSERT(server_.options().drainTime() >= remaining_time);
  const auto drain_time_count = drain_time.count();

  // If the user hasn't specified a drain timeout it will be zero, so we'll
  // confirm the drainClose immediately. Otherwise we'll use the drain timeout
  // as a modulus to a random number to salt the drain timing.
  if (drain_time_count == 0) {
    return true;
  }
  const auto elapsed_time = drain_time - remaining_time;
  return static_cast<uint64_t>(elapsed_time.count()) >
         (server_.api().randomGenerator().random() % drain_time_count);
}

void DrainManagerImpl::startDrainSequence(Network::DrainDirection direction,
                                          std::function<void()> drain_complete_cb) {
  ASSERT(direction != Network::DrainDirection::None, "a valid direction must be specified.");
  ASSERT(drain_tick_timers_.count(direction) == 0,
         "cannot run two drain sequences for the same direction.");
  auto new_timer = server_.dispatcher().createTimer(drain_complete_cb);
  const std::chrono::seconds drain_delay(server_.options().drainTime());
  new_timer->enableTimer(drain_delay);
  drain_tick_timers_[direction] = std::move(new_timer);
  // Note https://github.com/envoyproxy/envoy/issues/31457, previous to which,
  // drain_deadline_ was set *after* draining_ resulting in a read/write race between
  // the main thread running this function from admin, and the worker thread calling
  // drainClose. Note that drain_deadline_ is default-constructed which guarantees
  // to set the time-since epoch to a count of 0
  // (https://en.cppreference.com/w/cpp/chrono/time_point/time_point).
  ASSERT(drain_deadlines_[direction].time_since_epoch().count() == 0,
         "drain_deadline_ cannot be set twice for the same direction");
  // Since draining_ is atomic, it is safe to set drain_deadline_ without a mutex
  // as drain_close() only reads from drain_deadline_ if draining_ is true, and
  // C++ will not re-order an assign to an atomic. See
  // https://stackoverflow.com/questions/40320254/reordering-atomic-operations-in-c .
  drain_deadlines_[direction] = server_.dispatcher().timeSource().monotonicTime() + drain_delay;
  // Atomic assign must come after the assign to drain_deadline_.
  draining_.store(DrainPair{true, direction}, std::memory_order_seq_cst);
}

void DrainManagerImpl::startParentShutdownSequence() {
  // Do not initiate parent shutdown sequence when hot restart is disabled.
  if (server_.options().hotRestartDisabled()) {
    return;
  }
  ASSERT(!parent_shutdown_timer_);
  parent_shutdown_timer_ = server_.dispatcher().createTimer([this]() -> void {
    // Shut down the parent now. It should have already been draining.
    ENVOY_LOG(info, "shutting down parent after drain");
    server_.hotRestart().sendParentTerminateRequest();
  });

  parent_shutdown_timer_->enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(
      server_.options().parentShutdownTime()));
}

} // namespace Server
} // namespace Envoy

#include "server/drain_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/timer.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Server {

DrainManagerImpl::DrainManagerImpl(Instance& server,
                                   envoy::config::listener::v3::Listener::DrainType drain_type)
    : server_(server), drain_type_(drain_type) {}

bool DrainManagerImpl::drainClose() const {
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

  if (!draining_) {
    return false;
  }

  if (server_.options().drainStrategy() == Server::DrainStrategy::Immediate) {
    return true;
  }
  ASSERT(server_.options().drainStrategy() == Server::DrainStrategy::Gradual);

  // P(return true) = elapsed time / drain timeout
  // If the drain deadline is exceeded, skip the probability calculation.
  const MonotonicTime current_time = server_.dispatcher().timeSource().monotonicTime();
  if (current_time >= drain_deadline_) {
    return true;
  }

  const auto remaining_time =
      std::chrono::duration_cast<std::chrono::seconds>(drain_deadline_ - current_time);
  ASSERT(server_.options().drainTime() >= remaining_time);
  const auto elapsed_time = server_.options().drainTime() - remaining_time;
  return static_cast<uint64_t>(elapsed_time.count()) >
         (server_.api().randomGenerator().random() % server_.options().drainTime().count());
}

void DrainManagerImpl::startDrainSequence(std::function<void()> drain_complete_cb) {
  ASSERT(drain_complete_cb);
  ASSERT(!draining_);
  ASSERT(!drain_tick_timer_);
  draining_ = true;
  drain_tick_timer_ = server_.dispatcher().createTimer(drain_complete_cb);
  const std::chrono::seconds drain_delay(server_.options().drainTime());
  drain_tick_timer_->enableTimer(drain_delay);
  drain_deadline_ = server_.dispatcher().timeSource().monotonicTime() + drain_delay;
}

void DrainManagerImpl::startParentShutdownSequence() {
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

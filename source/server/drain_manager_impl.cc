#include "drain_manager_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"

#include "common/common/assert.h"

namespace Server {

const std::chrono::minutes DrainManagerImpl::DEFAULT_DRAIN_TIME{10};
const std::chrono::minutes DrainManagerImpl::DEFAULT_PARENT_SHUTDOWN_TIME{15};

DrainManagerImpl::DrainManagerImpl(Instance& server) : server_(server) {}

bool DrainManagerImpl::drainClose() {
  // If we are actively HC failed, always drain close.
  if (server_.healthCheckFailed()) {
    return true;
  }

  if (!draining()) {
    return false;
  }

  // We use the tick time as in increasing chance that we shutdown connections.
  return static_cast<uint64_t>(drain_time_completed_.count()) >
         (server_.random().random() % DEFAULT_DRAIN_TIME.count());
}

void DrainManagerImpl::drainSequenceTick() {
  log_trace("drain tick #{}", drain_time_completed_.count());
  ASSERT(drain_time_completed_ < DEFAULT_DRAIN_TIME);
  drain_time_completed_ += std::chrono::seconds(1);

  if (drain_time_completed_ < DEFAULT_DRAIN_TIME) {
    drain_tick_timer_->enableTimer(std::chrono::milliseconds(1000));
  }
}

void DrainManagerImpl::startDrainSequence() {
  ASSERT(!drain_tick_timer_);
  drain_tick_timer_ = server_.dispatcher().createTimer([this]() -> void { drainSequenceTick(); });
  drainSequenceTick();
}

void DrainManagerImpl::startParentShutdownSequence() {
  ASSERT(!parent_shutdown_timer_);
  parent_shutdown_timer_ = server_.dispatcher().createTimer([this]() -> void {
    // Shut down the parent now. It should have already been draining.
    log().info("shutting down parent after drain");
    server_.hotRestart().terminateParent();
  });

  parent_shutdown_timer_->enableTimer(
      std::chrono::duration_cast<std::chrono::milliseconds>(DEFAULT_PARENT_SHUTDOWN_TIME));
}

} // Server

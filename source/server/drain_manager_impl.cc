#include "source/server/drain_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Server {

DrainManagerImpl::DrainManagerImpl(Instance& server,
                                   envoy::config::listener::v3::Listener::DrainType drain_type,
                                   Event::Dispatcher& dispatcher)
    : server_(server), dispatcher_(dispatcher), drain_type_(drain_type),
      children_(Common::ThreadSafeCallbackManager::create()) {}

DrainManagerPtr
DrainManagerImpl::createChildManager(Event::Dispatcher& dispatcher,
                                     envoy::config::listener::v3::Listener::DrainType drain_type) {
  auto child = std::make_unique<DrainManagerImpl>(server_, drain_type, dispatcher);

  // Wire up the child so that when the parent starts draining, the child also sees the
  // state-change
  auto child_cb = children_->add(dispatcher, [this, child = child.get()] {
    // Not a double load since we first check the child drain pair and then this.draining_
    if (!child->draining_.load().first) {
      child->startDrainSequence(this->draining_.load().second, [] {});
    }
  });
  child->parent_callback_handle_ = std::move(child_cb);
  return child;
}

DrainManagerPtr DrainManagerImpl::createChildManager(Event::Dispatcher& dispatcher) {
  return createChildManager(dispatcher, drain_type_);
}

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

  auto current_drain = draining_.load();

  if (!current_drain.first) {
    return false;
  }

  // If the direction passed in is greater than the current draining direction
  // (e.g. direction = ALL, but draining_.second == INBOUND_ONLY), then don't
  // drain. We also don't want to drain if the direction is None (which doesn't really
  // make sense, but it's the correct behavior).
  if (direction == Network::DrainDirection::None || direction > current_drain.second) {
    return false;
  }

  if (server_.options().drainStrategy() == Server::DrainStrategy::Immediate) {
    return true;
  }
  ASSERT(server_.options().drainStrategy() == Server::DrainStrategy::Gradual);

  // P(return true) = elapsed time / drain timeout
  // If the drain deadline is exceeded, skip the probability calculation.
  const MonotonicTime current_time = dispatcher_.timeSource().monotonicTime();
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

Common::CallbackHandlePtr DrainManagerImpl::addOnDrainCloseCb(Network::DrainDirection direction,
                                                              DrainCloseCb cb) const {
  ASSERT(dispatcher_.isThreadSafe());
  auto current_drain = draining_.load();
  if (current_drain.first && direction <= current_drain.second) {
    const MonotonicTime current_time = dispatcher_.timeSource().monotonicTime();

    // Calculate the delay. If using an immediate drain-strategy or past our deadline, use
    // a zero millisecond delay. Otherwise, pick a random value within the remaining time-span.
    std::chrono::milliseconds drain_delay{0};
    if (server_.options().drainStrategy() != Server::DrainStrategy::Immediate) {
      if (current_time < drain_deadlines_.find(direction)->second) {
        const auto delta = drain_deadlines_.find(direction)->second - current_time;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

        // Note; current_time may be less than drain_deadline_ by only a
        // microsecond (delta will be 1000 nanoseconds), in which case when we
        // convert to milliseconds that will be 0, which will throw a SIGFPE
        // if used as a modulus unguarded.
        if (ms > 0) {
          drain_delay = std::chrono::milliseconds(server_.api().randomGenerator().random() % ms);
        }
      }
    }
    THROW_IF_NOT_OK(cb(drain_delay));
    return nullptr;
  }

  return cbs_.add(cb);
}

void DrainManagerImpl::addDrainCompleteCallback(Network::DrainDirection direction,
                                                std::function<void()> cb) {
  ASSERT(dispatcher_.isThreadSafe());
  auto drain_pair = draining_.load();
  ASSERT(drain_pair.first && direction <= drain_pair.second);

  // If the drain-tick-timer is active, add the callback to the queue. If not defined
  // then it must have already expired, invoke the callback immediately.
  if (drain_tick_timers_[direction]) {
    drain_complete_cbs_.push_back(cb);
  } else {
    cb();
  }
}

void DrainManagerImpl::startDrainSequence(Network::DrainDirection direction,
                                          std::function<void()> drain_complete_cb) {
  ASSERT(dispatcher_.isThreadSafe());
  ASSERT(drain_complete_cb);
  ASSERT(direction != Network::DrainDirection::None, "a valid direction must be specified.");
  auto current_drain = draining_.load();

  // If we've already started draining (either through direct invocation or through
  // parent-initiated draining), enqueue the drain_complete_cb and return
  if (current_drain.first && direction <= current_drain.second) {
    addDrainCompleteCallback(direction, drain_complete_cb);
    return;
  }
  ASSERT(drain_tick_timers_.count(direction) == 0,
         "cannot run two drain sequences for the same direction.");

  const std::chrono::seconds drain_delay(server_.options().drainTime());
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

  drain_deadlines_[direction] = dispatcher_.timeSource().monotonicTime() + drain_delay;
  // Atomic assign must come after the assign to drain_deadline_.
  draining_.store(DrainPair{true, direction}, std::memory_order_seq_cst);

  // Signal to child drain-managers to start their drain sequence
  children_->runCallbacks();
  // Schedule callback to run at end of drain time
  drain_tick_timers_[direction] = dispatcher_.createTimer([this, direction]() {
    for (auto& cb : drain_complete_cbs_) {
      cb();
    }
    drain_complete_cbs_.clear();
    drain_tick_timers_[direction].reset();
  });
  addDrainCompleteCallback(direction, drain_complete_cb);
  drain_tick_timers_[direction]->enableTimer(drain_delay);

  // Call registered on-drain callbacks - with gradual delays
  // Note: This will distribute drain events in the first 1/4th of the drain window
  //       to ensure that we initiate draining with enough time for graceful shutdowns.
  const MonotonicTime current_time = dispatcher_.timeSource().monotonicTime();
  std::chrono::seconds remaining_time{0};
  if (server_.options().drainStrategy() != Server::DrainStrategy::Immediate &&
      current_time < drain_deadlines_[direction]) {
    remaining_time = std::chrono::duration_cast<std::chrono::seconds>(drain_deadlines_[direction] -
                                                                      current_time);
    ASSERT(server_.options().drainTime() >= remaining_time);
  }

  uint32_t step_count = 0;
  size_t num_cbs = cbs_.size();
  THROW_IF_NOT_OK(cbs_.runCallbacksWith([&]() {
    // switch to floating-point math to avoid issues with integer division
    std::chrono::milliseconds delay{static_cast<int64_t>(
        static_cast<double>(step_count) / 4 / num_cbs *
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining_time).count())};
    step_count++;
    return delay;
  }));
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

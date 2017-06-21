#include "common/event/dispatched_thread.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"

#include "common/api/api_impl.h"
#include "common/common/thread.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Event {

void DispatchedThreadImpl::start(Server::GuardDog& guard_dog) {
  // Silly trick for preventing the event loop from exiting when there are no
  // events: keep a long duration timer going in perpetuity.
  auto no_exit = [this]() -> void {
    no_exit_timer_->enableTimer(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)));
  };
  no_exit_timer_ = dispatcher_->createTimer(no_exit);

  thread_.reset(new Thread::Thread([this, &guard_dog]() -> void { threadRoutine(guard_dog); }));
}

void DispatchedThreadImpl::exit() {
  if (thread_) {
    dispatcher_->exit();
    thread_->join();
  }
}

void DispatchedThreadImpl::threadRoutine(Server::GuardDog& guard_dog) {
  ENVOY_LOG(info, "dispatched thread entering dispatch loop");
  auto watchdog = guard_dog.createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(*dispatcher_);
  dispatcher_->run(Dispatcher::RunType::Block);
  ENVOY_LOG(info, "dispatched thread exited dispatch loop");
  guard_dog.stopWatching(watchdog);

  no_exit_timer_.reset();
  watchdog.reset();
  dispatcher_.reset();
}

} // Event
} // Envoy

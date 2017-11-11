#include "common/event/dispatched_thread.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"

#include "common/common/thread.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Event {

void DispatchedThreadImpl::start(Server::GuardDog& guard_dog) {
  thread_.reset(new Thread::Thread([this, &guard_dog]() -> void { threadRoutine(guard_dog); }));
}

void DispatchedThreadImpl::exit() {
  if (thread_) {
    dispatcher_->exit();
    thread_->join();
  }
}

void DispatchedThreadImpl::threadRoutine(Server::GuardDog& guard_dog) {
  ENVOY_LOG(debug, "dispatched thread entering dispatch loop");
  auto watchdog = guard_dog.createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(*dispatcher_);
  dispatcher_->run(Dispatcher::RunType::Block);
  ENVOY_LOG(debug, "dispatched thread exited dispatch loop");
  guard_dog.stopWatching(watchdog);

  watchdog.reset();
  dispatcher_.reset();
}

} // namespace Event
} // namespace Envoy

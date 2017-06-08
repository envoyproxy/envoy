#include "server/worker.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"
#include "envoy/thread_local/thread_local.h"

#include "common/api/api_impl.h"
#include "common/common/thread.h"

namespace Envoy {
Worker::Worker(ThreadLocal::Instance& tls, std::chrono::milliseconds file_flush_interval_msec)
    : tls_(tls), handler_(new Server::ConnectionHandlerImpl(
                     log(), Api::ApiPtr{new Api::Impl(file_flush_interval_msec)})) {
  tls_.registerThread(handler_->dispatcher(), false);
}

Worker::~Worker() {}

void Worker::initializeConfiguration(Server::Configuration::Main& config,
                                     const SocketMap& socket_map, Server::GuardDog& guard_dog) {
  for (const Server::Configuration::ListenerPtr& listener : config.listeners()) {
    const Network::ListenerOptions listener_options = {
        .bind_to_port_ = listener->bindToPort(),
        .use_proxy_proto_ = listener->useProxyProto(),
        .use_original_dst_ = listener->useOriginalDst(),
        .per_connection_buffer_limit_bytes_ = listener->perConnectionBufferLimitBytes()};
    if (listener->sslContext()) {
      handler_->addSslListener(listener->filterChainFactory(), *listener->sslContext(),
                               *socket_map.at(listener.get()), listener->scope(), listener_options);
    } else {
      handler_->addListener(listener->filterChainFactory(), *socket_map.at(listener.get()),
                            listener->scope(), listener_options);
    }
  }

  // This is just a hack to prevent the event loop from exiting until we tell it to. By default it
  // exits if there are no pending events.
  no_exit_timer_ = handler_->dispatcher().createTimer([this]() -> void { onNoExitTimer(); });
  onNoExitTimer();

  thread_.reset(new Thread::Thread([this, &guard_dog]() -> void { threadRoutine(guard_dog); }));
}

void Worker::exit() {
  // It's possible for the server to cleanly shut down while cluster initialization during startup
  // is happening, so we might not yet have a thread.
  if (thread_) {
    handler_->dispatcher().exit();
    thread_->join();
  }
}

void Worker::onNoExitTimer() {
  no_exit_timer_->enableTimer(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)));
}

void Worker::threadRoutine(Server::GuardDog& guard_dog) {
  log_facility(info, "worker entering dispatch loop");
  auto watchdog = guard_dog.createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(handler_->dispatcher());
  handler_->dispatcher().run(Event::Dispatcher::RunType::Block);
  log_facility(info, "worker exited dispatch loop");
  guard_dog.stopWatching(watchdog);

  // We must close all active connections before we actually exit the thread. This prevents any
  // destructors from running on the main thread which might reference thread locals. Destroying
  // the handler does this as well as destroying the dispatcher which purges the delayed deletion
  // list.
  handler_->closeConnections();
  tls_.shutdownThread();
  no_exit_timer_.reset();
  watchdog.reset();
  handler_.reset();
}
} // Envoy

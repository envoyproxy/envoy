#include "server/worker_impl.h"

#include <functional>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/exception.h"
#include "envoy/server/configuration.h"
#include "envoy/thread_local/thread_local.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

WorkerPtr ProdWorkerFactory::createWorker(OverloadManager& overload_manager,
                                          const std::string& worker_name) {
  Event::DispatcherPtr dispatcher(api_.allocateDispatcher(worker_name));
  return WorkerPtr{
      new WorkerImpl(tls_, hooks_, std::move(dispatcher),
                     Network::ConnectionHandlerPtr{new ConnectionHandlerImpl(*dispatcher)},
                     overload_manager, api_)};
}

WorkerImpl::WorkerImpl(ThreadLocal::Instance& tls, ListenerHooks& hooks,
                       Event::DispatcherPtr&& dispatcher, Network::ConnectionHandlerPtr handler,
                       OverloadManager& overload_manager, Api::Api& api)
    : tls_(tls), hooks_(hooks), dispatcher_(std::move(dispatcher)), handler_(std::move(handler)),
      api_(api) {
  tls_.registerThread(*dispatcher_, false);
  overload_manager.registerForAction(
      OverloadActionNames::get().StopAcceptingConnections, *dispatcher_,
      [this](OverloadActionState state) { stopAcceptingConnectionsCb(state); });
}

void WorkerImpl::addListener(absl::optional<uint64_t> overridden_listener,
                             Network::ListenerConfig& listener, AddListenerCompletion completion) {
  // All listener additions happen via post. However, we must deal with the case where the listener
  // can not be created on the worker. There is a race condition where 2 processes can successfully
  // bind to an address, but then fail to listen() with EADDRINUSE. During initial startup, we want
  // to surface this.
  dispatcher_->post([this, overridden_listener, &listener, completion]() -> void {
    try {
      handler_->addListener(overridden_listener, listener);
      hooks_.onWorkerListenerAdded();
      completion(true);
    } catch (const Network::CreateListenerException& e) {
      ENVOY_LOG(error, "failed to add listener on worker: {}", e.what());
      completion(false);
    }
  });
}

uint64_t WorkerImpl::numConnections() const {
  uint64_t ret = 0;
  if (handler_) {
    ret = handler_->numConnections();
  }
  return ret;
}

void WorkerImpl::removeListener(Network::ListenerConfig& listener,
                                std::function<void()> completion) {
  ASSERT(thread_);
  const uint64_t listener_tag = listener.listenerTag();
  dispatcher_->post([this, listener_tag, completion]() -> void {
    handler_->removeListeners(listener_tag);
    completion();
    hooks_.onWorkerListenerRemoved();
  });
}

void WorkerImpl::removeFilterChains(uint64_t listener_tag,
                                    const std::list<const Network::FilterChain*>& filter_chains,
                                    std::function<void()> completion) {
  ASSERT(thread_);
  dispatcher_->post(
      [this, listener_tag, &filter_chains, completion = std::move(completion)]() -> void {
        handler_->removeFilterChains(listener_tag, filter_chains, completion);
      });
}

void WorkerImpl::start(GuardDog& guard_dog) {
  ASSERT(!thread_);

  // In posix, thread names are limited to 15 characters, so contrive to make
  // sure all interesting data fits there. The naming occurs in
  // ListenerManagerImpl's constructor: absl::StrCat("worker_", i). Let's say we
  // have 9999 threads. We'd need, so we need 7 bytes for "worker_", 4 bytes
  // for the thread index, leaving us 4 bytes left to distinguish between the
  // two threads used per dispatcher. We'll call this one "dsp:" and the
  // one allocated in guarddog_impl.cc "dog:".
  //
  // TODO(jmarantz): consider refactoring how this naming works so this naming
  // architecture is centralized, resulting in clearer names.
  Thread::Options options{absl::StrCat("wrk:", dispatcher_->name())};
  thread_ = api_.threadFactory().createThread(
      [this, &guard_dog]() -> void { threadRoutine(guard_dog); }, options);
}

void WorkerImpl::initializeStats(Stats::Scope& scope) { dispatcher_->initializeStats(scope); }

void WorkerImpl::stop() {
  // It's possible for the server to cleanly shut down while cluster initialization during startup
  // is happening, so we might not yet have a thread.
  if (thread_) {
    dispatcher_->exit();
    thread_->join();
  }
}

void WorkerImpl::stopListener(Network::ListenerConfig& listener, std::function<void()> completion) {
  ASSERT(thread_);
  const uint64_t listener_tag = listener.listenerTag();
  dispatcher_->post([this, listener_tag, completion]() -> void {
    handler_->stopListeners(listener_tag);
    if (completion != nullptr) {
      completion();
    }
  });
}

void WorkerImpl::threadRoutine(GuardDog& guard_dog) {
  ENVOY_LOG(debug, "worker entering dispatch loop");
  // The watch dog must be created after the dispatcher starts running and has post events flushed,
  // as this is when TLS stat scopes start working.
  dispatcher_->post([this, &guard_dog]() {
    watch_dog_ =
        guard_dog.createWatchDog(api_.threadFactory().currentThreadId(), dispatcher_->name());
    watch_dog_->startWatchdog(*dispatcher_);
  });
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(debug, "worker exited dispatch loop");
  guard_dog.stopWatching(watch_dog_);

  // We must close all active connections before we actually exit the thread. This prevents any
  // destructors from running on the main thread which might reference thread locals. Destroying
  // the handler does this which additionally purges the dispatcher delayed deletion list.
  handler_.reset();
  tls_.shutdownThread();
  watch_dog_.reset();
}

void WorkerImpl::stopAcceptingConnectionsCb(OverloadActionState state) {
  switch (state) {
  case OverloadActionState::Active:
    handler_->disableListeners();
    break;
  case OverloadActionState::Inactive:
    handler_->enableListeners();
    break;
  }
}

} // namespace Server
} // namespace Envoy

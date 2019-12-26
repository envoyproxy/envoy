#include "server/worker_impl.h"

#include <functional>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"
#include "envoy/thread_local/thread_local.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

WorkerPtr ProdWorkerFactory::createWorker(OverloadManager& overload_manager,
                                          const std::string& worker_name) {
  Event::DispatcherPtr dispatcher(api_.allocateDispatcher());
  return WorkerPtr{new WorkerImpl(
      tls_, hooks_, std::move(dispatcher),
      Network::ConnectionHandlerPtr{new ConnectionHandlerImpl(*dispatcher, worker_name)},
      overload_manager, api_, worker_name)};
}

WorkerImpl::WorkerImpl(ThreadLocal::Instance& tls, ListenerHooks& hooks,
                       Event::DispatcherPtr&& dispatcher, Network::ConnectionHandlerPtr handler,
                       OverloadManager& overload_manager, Api::Api& api,
                       const std::string& worker_name)
    : tls_(tls), hooks_(hooks), dispatcher_(std::move(dispatcher)), handler_(std::move(handler)),
      api_(api), worker_name_(worker_name) {
  tls_.registerThread(*dispatcher_, false);
  overload_manager.registerForAction(
      OverloadActionNames::get().StopAcceptingConnections, *dispatcher_,
      [this](OverloadActionState state) { stopAcceptingConnectionsCb(state); });
}

void WorkerImpl::addListener(Network::ListenerConfig& listener, AddListenerCompletion completion) {
  // All listener additions happen via post. However, we must deal with the case where the listener
  // can not be created on the worker. There is a race condition where 2 processes can successfully
  // bind to an address, but then fail to listen() with EADDRINUSE. During initial startup, we want
  // to surface this.
  dispatcher_->post([this, &listener, completion]() -> void {
    try {
      handler_->addListener(listener);
      hooks_.onWorkerListenerAdded();
      completion(true);
    } catch (const Network::CreateListenerException& e) {
      completion(false);
    }
  });
}

uint64_t WorkerImpl::numConnections() {
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

void WorkerImpl::start(GuardDog& guard_dog) {
  ASSERT(!thread_);
  thread_ =
      api_.threadFactory().createThread([this, &guard_dog]() -> void { threadRoutine(guard_dog); });
}

void WorkerImpl::initializeStats(Stats::Scope& scope, const std::string& prefix) {
  dispatcher_->initializeStats(scope, prefix);
}

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
    watch_dog_ = guard_dog.createWatchDog(api_.threadFactory().currentThreadId(), worker_name_);
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

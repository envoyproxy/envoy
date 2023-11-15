#include "source/server/worker_impl.h"

#include <functional>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/exception.h"
#include "envoy/server/configuration.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/config/utility.h"
#include "source/server/listener_manager_factory.h"

namespace Envoy {
namespace Server {
namespace {

std::unique_ptr<ConnectionHandler> getHandler(Event::Dispatcher& dispatcher, uint32_t index,
                                              OverloadManager& overload_manager) {

  auto* factory = Config::Utility::getFactoryByName<ConnectionHandlerFactory>(
      "envoy.connection_handler.default");
  if (factory) {
    return factory->createConnectionHandler(dispatcher, index, overload_manager);
  }
  ENVOY_LOG_MISC(debug, "Unable to find envoy.connection_handler.default factory");
  return nullptr;
}

} // namespace

WorkerPtr ProdWorkerFactory::createWorker(uint32_t index, OverloadManager& overload_manager,
                                          const std::string& worker_name) {
  Event::DispatcherPtr dispatcher(
      api_.allocateDispatcher(worker_name, overload_manager.scaledTimerFactory()));
  auto conn_handler = getHandler(*dispatcher, index, overload_manager);
  return std::make_unique<WorkerImpl>(tls_, hooks_, std::move(dispatcher), std::move(conn_handler),
                                      overload_manager, api_, stat_names_);
}

WorkerImpl::WorkerImpl(ThreadLocal::Instance& tls, ListenerHooks& hooks,
                       Event::DispatcherPtr&& dispatcher, Network::ConnectionHandlerPtr handler,
                       OverloadManager& overload_manager, Api::Api& api,
                       WorkerStatNames& stat_names)
    : tls_(tls), hooks_(hooks), dispatcher_(std::move(dispatcher)), handler_(std::move(handler)),
      api_(api), reset_streams_counter_(
                     api_.rootScope().counterFromStatName(stat_names.reset_high_memory_stream_)) {
  tls_.registerThread(*dispatcher_, false);
  overload_manager.registerForAction(
      OverloadActionNames::get().StopAcceptingConnections, *dispatcher_,
      [this](OverloadActionState state) { stopAcceptingConnectionsCb(state); });
  overload_manager.registerForAction(
      OverloadActionNames::get().RejectIncomingConnections, *dispatcher_,
      [this](OverloadActionState state) { rejectIncomingConnectionsCb(state); });
  overload_manager.registerForAction(
      OverloadActionNames::get().ResetStreams, *dispatcher_,
      [this](OverloadActionState state) { resetStreamsUsingExcessiveMemory(state); });
}

void WorkerImpl::addListener(absl::optional<uint64_t> overridden_listener,
                             Network::ListenerConfig& listener, AddListenerCompletion completion,
                             Runtime::Loader& runtime, Random::RandomGenerator& random) {
  dispatcher_->post(
      [this, overridden_listener, &listener, &runtime, &random, completion]() -> void {
        handler_->addListener(overridden_listener, listener, runtime, random);
        hooks_.onWorkerListenerAdded();
        completion();
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

void WorkerImpl::start(OptRef<GuardDog> guard_dog, const std::function<void()>& cb) {
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
      [this, guard_dog, cb]() -> void { threadRoutine(guard_dog, cb); }, options);
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

void WorkerImpl::stopListener(Network::ListenerConfig& listener,
                              const Network::ExtraShutdownListenerOptions& options,
                              std::function<void()> completion) {
  const uint64_t listener_tag = listener.listenerTag();
  dispatcher_->post([this, listener_tag, options, completion]() -> void {
    handler_->stopListeners(listener_tag, options);
    if (completion != nullptr) {
      completion();
    }
  });
}

void WorkerImpl::threadRoutine(OptRef<GuardDog> guard_dog, const std::function<void()>& cb) {
  ENVOY_LOG(debug, "worker entering dispatch loop");
  // The watch dog must be created after the dispatcher starts running and has post events flushed,
  // as this is when TLS stat scopes start working.
  dispatcher_->post([this, &guard_dog, cb]() {
    cb();
    if (guard_dog.has_value()) {
      watch_dog_ = guard_dog->createWatchDog(api_.threadFactory().currentThreadId(),
                                             dispatcher_->name(), *dispatcher_);
    }
  });
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(debug, "worker exited dispatch loop");
  if (guard_dog.has_value()) {
    guard_dog->stopWatching(watch_dog_);
  }
  dispatcher_->shutdown();

  // We must close all active connections before we actually exit the thread. This prevents any
  // destructors from running on the main thread which might reference thread locals. Destroying
  // the handler does this which additionally purges the dispatcher delayed deletion list.
  handler_.reset();
  tls_.shutdownThread();
  watch_dog_.reset();
}

void WorkerImpl::stopAcceptingConnectionsCb(OverloadActionState state) {
  if (state.isSaturated()) {
    handler_->disableListeners();
  } else {
    handler_->enableListeners();
  }
}

void WorkerImpl::rejectIncomingConnectionsCb(OverloadActionState state) {
  handler_->setListenerRejectFraction(state.value());
}

void WorkerImpl::resetStreamsUsingExcessiveMemory(OverloadActionState state) {
  uint64_t streams_reset_count =
      dispatcher_->getWatermarkFactory().resetAccountsGivenPressure(state.value().value());
  reset_streams_counter_.add(streams_reset_count);
}

} // namespace Server
} // namespace Envoy

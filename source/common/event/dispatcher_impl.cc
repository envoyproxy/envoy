#include "common/event/dispatcher_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/event/file_event_impl.h"
#include "common/event/libevent_scheduler.h"
#include "common/event/signal_impl.h"
#include "common/event/timer_impl.h"
#include "common/filesystem/watcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/tcp_listener_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/runtime/runtime_features.h"

#include "event2/event.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif

#ifdef __APPLE__
#include "common/network/apple_dns_impl.h"
#endif

namespace Envoy {
namespace Event {

DispatcherImplBase::DispatcherImplBase(Api::Api& api, TimeSystem& time_system)
    : api_(api), scheduler_(time_system.createScheduler(base_scheduler_, base_scheduler_)) {}

FileEventPtr DispatcherImplBase::createFileEvent(os_fd_t fd, FileReadyCb cb,
                                                 FileTriggerType trigger, uint32_t events) {
  ASSERT(isThreadSafe());
  return FileEventPtr{new FileEventImpl(
      *this, fd,
      [this, cb](uint32_t events) {
        touchWatchdog();
        cb(events);
      },
      trigger, events)};
}

TimerPtr DispatcherImplBase::createTimer(TimerCb cb) {
  ASSERT(isThreadSafe());
  return scheduler_->createTimer(
      [this, cb]() {
        touchWatchdog();
        cb();
      },
      *this);
}

Event::SchedulableCallbackPtr
DispatcherImplBase::createSchedulableCallback(std::function<void()> cb) {
  return createRawSchedulableCallback([this, cb = std::move(cb)] {
    touchWatchdog();
    cb();
  });
}

const ScopeTrackedObject* DispatcherImplBase::setTrackedObject(const ScopeTrackedObject* object) {
  const ScopeTrackedObject* return_object = current_object_;
  current_object_ = object;
  return return_object;
}

Event::SchedulableCallbackPtr
DispatcherImplBase::createRawSchedulableCallback(std::function<void()> cb) {
  return base_scheduler_.createSchedulableCallback(std::move(cb));
}

void DispatcherImplBase::registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                                          std::chrono::milliseconds min_touch_interval) {
  ASSERT(!watchdog_registration_, "Each dispatcher can have at most one registered watchdog.");
  watchdog_registration_.emplace(watchdog, *scheduler_, min_touch_interval, *this);
}

void DispatcherImplBase::touchWatchdog() {
  if (watchdog_registration_) {
    watchdog_registration_->touchWatchdog();
  }
}

void DispatcherImplBase::runFatalActionsOnTrackedObject(
    const FatalAction::FatalActionPtrList& actions) const {
  // Only run if this is the dispatcher of the current thread and
  // DispatcherImpl::Run has been called.
  if (run_tid_.isEmpty() || (run_tid_ != api_.threadFactory().currentThreadId())) {
    return;
  }

  for (const auto& action : actions) {
    action->run(current_object_);
  }
}

void DispatcherImplBase::onFatalError(std::ostream& os) const {
  // Dump the state of the tracked object if it is in the current thread. This generally results
  // in dumping the active state only for the thread which caused the fatal error.
  if (isThreadSafe()) {
    if (current_object_) {
      current_object_->dumpState(os);
    }
  }
}

DispatcherImplBase::WatchdogRegistration::WatchdogRegistration(
    const Server::WatchDogSharedPtr& watchdog, Scheduler& scheduler,
    std::chrono::milliseconds timer_interval, DispatcherBase& dispatcher)
    : watchdog_(watchdog), timer_interval_(timer_interval) {
  touch_timer_ = scheduler.createTimer(
      [this]() -> void {
        watchdog_->touch();
        touch_timer_->enableTimer(timer_interval_);
      },
      dispatcher);
  touch_timer_->enableTimer(timer_interval_);
}

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system,
                               const Buffer::WatermarkFactorySharedPtr& factory)
    : base_(api, time_system), name_(name),
      buffer_factory_(factory != nullptr ? factory
                                         : std::make_shared<Buffer::WatermarkBufferFactory>()),
      deferred_delete_cb_(
          base_.createRawSchedulableCallback([this]() -> void { clearDeferredDeleteList(); })),
      post_cb_(base_.createRawSchedulableCallback([this]() -> void { runPostCallbacks(); })),
      current_to_delete_(&to_delete_1_) {
  ASSERT(!name_.empty());
  FatalErrorHandler::registerFatalErrorHandler(*this);
  base_.updateApproximateMonotonicTime();
  base_.registerOnPrepareCallback(std::bind(&DispatcherImpl::updateApproximateMonotonicTime, this));
}

DispatcherImpl::~DispatcherImpl() { FatalErrorHandler::removeFatalErrorHandler(*this); }

void DispatcherImpl::registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                                      std::chrono::milliseconds min_touch_interval) {
  base_.registerWatchdog(watchdog, min_touch_interval);
}

void DispatcherImpl::initializeStats(Stats::Scope& scope,
                                     const absl::optional<std::string>& prefix) {
  const std::string effective_prefix = prefix.has_value() ? *prefix : absl::StrCat(name_, ".");
  // This needs to be run in the dispatcher's thread, so that we have a thread id to log.
  post([this, &scope, effective_prefix] {
    stats_prefix_ = effective_prefix + "dispatcher";
    stats_ = std::make_unique<DispatcherStats>(
        DispatcherStats{ALL_DISPATCHER_STATS(POOL_HISTOGRAM_PREFIX(scope, stats_prefix_ + "."))});
    base_.initializeStats(stats_.get());
    ENVOY_LOG(debug, "running {} on thread {}", stats_prefix_, base_.run_tid().debugString());
  });
}

void DispatcherImpl::clearDeferredDeleteList() {
  ASSERT(isThreadSafe());
  std::vector<DeferredDeletablePtr>* to_delete = current_to_delete_;

  size_t num_to_delete = to_delete->size();
  if (deferred_deleting_ || !num_to_delete) {
    return;
  }

  ENVOY_LOG(trace, "clearing deferred deletion list (size={})", num_to_delete);

  // Swap the current deletion vector so that if we do deferred delete while we are deleting, we
  // use the other vector. We will get another callback to delete that vector.
  if (current_to_delete_ == &to_delete_1_) {
    current_to_delete_ = &to_delete_2_;
  } else {
    current_to_delete_ = &to_delete_1_;
  }

  base_.touchWatchdog();
  deferred_deleting_ = true;

  // Calling clear() on the vector does not specify which order destructors run in. We want to
  // destroy in FIFO order so just do it manually. This required 2 passes over the vector which is
  // not optimal but can be cleaned up later if needed.
  for (size_t i = 0; i < num_to_delete; i++) {
    (*to_delete)[i].reset();
  }

  to_delete->clear();
  deferred_deleting_ = false;
}

Network::ServerConnectionPtr
DispatcherImpl::createServerConnection(Network::ConnectionSocketPtr&& socket,
                                       Network::TransportSocketPtr&& transport_socket,
                                       StreamInfo::StreamInfo& stream_info) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::ServerConnectionImpl>(
      *this, std::move(socket), std::move(transport_socket), stream_info, true);
}

Network::ClientConnectionPtr
DispatcherImpl::createClientConnection(Network::Address::InstanceConstSharedPtr address,
                                       Network::Address::InstanceConstSharedPtr source_address,
                                       Network::TransportSocketPtr&& transport_socket,
                                       const Network::ConnectionSocket::OptionsSharedPtr& options) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::ClientConnectionImpl>(*this, address, source_address,
                                                         std::move(transport_socket), options);
}

Network::DnsResolverSharedPtr DispatcherImpl::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    const bool use_tcp_for_dns_lookups) {
  ASSERT(isThreadSafe());
#ifdef __APPLE__
  static bool use_apple_api_for_dns_lookups =
      Runtime::runtimeFeatureEnabled("envoy.restart_features.use_apple_api_for_dns_lookups");
  if (use_apple_api_for_dns_lookups) {
    RELEASE_ASSERT(
        resolvers.empty(),
        "defining custom resolvers is not possible when using Apple APIs for DNS resolution. "
        "Apple's API only allows overriding DNS resolvers via system settings. Delete resolvers "
        "config or disable the envoy.restart_features.use_apple_api_for_dns_lookups runtime "
        "feature.");
    RELEASE_ASSERT(!use_tcp_for_dns_lookups,
                   "using TCP for DNS lookups is not possible when using Apple APIs for DNS "
                   "resolution. Apple' API only uses UDP for DNS resolution. Use UDP or disable "
                   "the envoy.restart_features.use_apple_api_for_dns_lookups runtime feature.");
    return std::make_shared<Network::AppleDnsResolverImpl>(*this, base_.api().randomGenerator(),
                                                           base_.api().rootScope());
  }
#endif
  return std::make_shared<Network::DnsResolverImpl>(*this, resolvers, use_tcp_for_dns_lookups);
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  ASSERT(isThreadSafe());
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this, base_.api())};
}

Network::ListenerPtr DispatcherImpl::createListener(Network::SocketSharedPtr&& socket,
                                                    Network::TcpListenerCallbacks& cb,
                                                    bool bind_to_port, uint32_t backlog_size) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::TcpListenerImpl>(
      *this, base_.api().randomGenerator(), std::move(socket), cb, bind_to_port, backlog_size);
}

Network::UdpListenerPtr DispatcherImpl::createUdpListener(Network::SocketSharedPtr socket,
                                                          Network::UdpListenerCallbacks& cb) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::UdpListenerImpl>(*this, std::move(socket), cb, timeSource());
}

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  ASSERT(isThreadSafe());
  current_to_delete_->emplace_back(std::move(to_delete));
  ENVOY_LOG(trace, "item added to deferred deletion list (size={})", current_to_delete_->size());
  if (current_to_delete_->size() == 1) {
    deferred_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

SignalEventPtr DispatcherImpl::listenForSignal(signal_t signal_num, SignalCb cb) {
  ASSERT(isThreadSafe());
  return SignalEventPtr{new SignalEventImpl(base_, signal_num, cb)};
}

void DispatcherImpl::post(std::function<void()> callback) {
  bool do_post;
  {
    Thread::LockGuard lock(post_lock_);
    do_post = post_callbacks_.empty();
    post_callbacks_.push_back(callback);
  }

  if (do_post) {
    post_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::run(RunType type) {
  base_.saveTid();

  // Flush all post callbacks before we run the event loop. We do this because there are post
  // callbacks that have to get run before the initial event loop starts running. libevent does
  // not guarantee that events are run in any particular order. So even if we post() and call
  // event_base_once() before some other event, the other event might get called first.
  runPostCallbacks();
  base_.run(type);
}

void DispatcherImpl::runPostCallbacks() {
  // Clear the deferred delete list before running post callbacks to reduce non-determinism in
  // callback processing, and more easily detect if a scheduled post callback refers to one of the
  // objects that is being deferred deleted.
  clearDeferredDeleteList();

  std::list<std::function<void()>> callbacks;
  {
    // Take ownership of the callbacks under the post_lock_. The lock must be released before
    // callbacks execute. Callbacks added after this transfer will re-arm post_cb_ and will execute
    // later in the event loop.
    Thread::LockGuard lock(post_lock_);
    callbacks = std::move(post_callbacks_);
    // post_callbacks_ should be empty after the move.
    ASSERT(post_callbacks_.empty());
  }
  // It is important that the execution and deletion of the callback happen while post_lock_ is not
  // held. Either the invocation or destructor of the callback can call post() on this dispatcher.
  while (!callbacks.empty()) {
    // Touch the watchdog before executing the callback to avoid spurious watchdog miss events when
    // executing a long list of callbacks.
    base_.touchWatchdog();
    // Run the callback.
    callbacks.front()();
    // Pop the front so that the destructor of the callback that just executed runs before the next
    // callback executes.
    callbacks.pop_front();
  }
}

void DispatcherImpl::runFatalActionsOnTrackedObject(
    const FatalAction::FatalActionPtrList& actions) const {
  base_.runFatalActionsOnTrackedObject(actions);
}

} // namespace Event
} // namespace Envoy

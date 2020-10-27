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

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system)
    : DispatcherImpl(name, std::make_unique<Buffer::WatermarkBufferFactory>(), api, time_system) {}

DispatcherImpl::DispatcherImpl(const std::string& name, Buffer::WatermarkFactoryPtr&& factory,
                               Api::Api& api, Event::TimeSystem& time_system)
    : name_(name), api_(api), buffer_factory_(std::move(factory)),
      scheduler_(time_system.createScheduler(base_scheduler_, base_scheduler_)),
      deferred_delete_cb_(base_scheduler_.createSchedulableCallback(
          [this]() -> void { clearDeferredDeleteList(); })),
      post_cb_(base_scheduler_.createSchedulableCallback([this]() -> void { runPostCallbacks(); })),
      current_to_delete_(&to_delete_1_) {
  ASSERT(!name_.empty());
  FatalErrorHandler::registerFatalErrorHandler(*this);
  updateApproximateMonotonicTimeInternal();
  base_scheduler_.registerOnPrepareCallback(
      std::bind(&DispatcherImpl::updateApproximateMonotonicTime, this));
}

DispatcherImpl::~DispatcherImpl() { FatalErrorHandler::removeFatalErrorHandler(*this); }

void DispatcherImpl::initializeStats(Stats::Scope& scope,
                                     const absl::optional<std::string>& prefix) {
  const std::string effective_prefix = prefix.has_value() ? *prefix : absl::StrCat(name_, ".");
  // This needs to be run in the dispatcher's thread, so that we have a thread id to log.
  post([this, &scope, effective_prefix] {
    stats_prefix_ = effective_prefix + "dispatcher";
    stats_ = std::make_unique<DispatcherStats>(
        DispatcherStats{ALL_DISPATCHER_STATS(POOL_HISTOGRAM_PREFIX(scope, stats_prefix_ + "."))});
    base_scheduler_.initializeStats(stats_.get());
    ENVOY_LOG(debug, "running {} on thread {}", stats_prefix_, run_tid_.debugString());
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
    return Network::DnsResolverSharedPtr{new Network::AppleDnsResolverImpl(*this)};
  }
#endif
  return Network::DnsResolverSharedPtr{
      new Network::DnsResolverImpl(*this, resolvers, use_tcp_for_dns_lookups)};
}

FileEventPtr DispatcherImpl::createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                                             uint32_t events) {
  ASSERT(isThreadSafe());
  return FileEventPtr{new FileEventImpl(*this, fd, cb, trigger, events)};
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  ASSERT(isThreadSafe());
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this, api_)};
}

Network::ListenerPtr DispatcherImpl::createListener(Network::SocketSharedPtr&& socket,
                                                    Network::TcpListenerCallbacks& cb,
                                                    bool bind_to_port, uint32_t backlog_size) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::TcpListenerImpl>(
      *this, api_.randomGenerator(), std::move(socket), cb, bind_to_port, backlog_size);
}

Network::UdpListenerPtr DispatcherImpl::createUdpListener(Network::SocketSharedPtr socket,
                                                          Network::UdpListenerCallbacks& cb) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::UdpListenerImpl>(*this, std::move(socket), cb, timeSource());
}

TimerPtr DispatcherImpl::createTimer(TimerCb cb) {
  ASSERT(isThreadSafe());
  return createTimerInternal(cb);
}

Event::SchedulableCallbackPtr DispatcherImpl::createSchedulableCallback(std::function<void()> cb) {
  ASSERT(isThreadSafe());
  return base_scheduler_.createSchedulableCallback(cb);
}

TimerPtr DispatcherImpl::createTimerInternal(TimerCb cb) {
  return scheduler_->createTimer(cb, *this);
}

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  ASSERT(isThreadSafe());
  current_to_delete_->emplace_back(std::move(to_delete));
  ENVOY_LOG(trace, "item added to deferred deletion list (size={})", current_to_delete_->size());
  if (current_to_delete_->size() == 1) {
    deferred_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::exit() { base_scheduler_.loopExit(); }

SignalEventPtr DispatcherImpl::listenForSignal(int signal_num, SignalCb cb) {
  ASSERT(isThreadSafe());
  return SignalEventPtr{new SignalEventImpl(*this, signal_num, cb)};
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
  run_tid_ = api_.threadFactory().currentThreadId();

  // Flush all post callbacks before we run the event loop. We do this because there are post
  // callbacks that have to get run before the initial event loop starts running. libevent does
  // not guarantee that events are run in any particular order. So even if we post() and call
  // event_base_once() before some other event, the other event might get called first.
  runPostCallbacks();
  base_scheduler_.run(type);
}

MonotonicTime DispatcherImpl::approximateMonotonicTime() const {
  return approximate_monotonic_time_;
}

void DispatcherImpl::updateApproximateMonotonicTime() { updateApproximateMonotonicTimeInternal(); }

void DispatcherImpl::updateApproximateMonotonicTimeInternal() {
  approximate_monotonic_time_ = api_.timeSource().monotonicTime();
}

void DispatcherImpl::runPostCallbacks() {
  while (true) {
    // It is important that this declaration is inside the body of the loop so that the callback is
    // destructed while post_lock_ is not held. If callback is declared outside the loop and reused
    // for each iteration, the previous iteration's callback is destructed when callback is
    // re-assigned, which happens while holding the lock. This can lead to a deadlock (via
    // recursive mutex acquisition) if destroying the callback runs a destructor, which through some
    // callstack calls post() on this dispatcher.
    std::function<void()> callback;
    {
      Thread::LockGuard lock(post_lock_);
      if (post_callbacks_.empty()) {
        return;
      }
      callback = post_callbacks_.front();
      post_callbacks_.pop_front();
    }
    callback();
  }
}

} // namespace Event
} // namespace Envoy

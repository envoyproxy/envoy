#include "source/common/event/dispatcher_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/network/client_connection_factory.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/event/file_event_impl.h"
#include "source/common/event/libevent_scheduler.h"
#include "source/common/event/scaled_range_timer_manager_impl.h"
#include "source/common/event/signal_impl.h"
#include "source/common/event/timer_impl.h"
#include "source/common/filesystem/watcher_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "event2/event.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#endif

namespace Envoy {
namespace Event {

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system)
    : DispatcherImpl(name, api, time_system, {}) {}

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system,
                               const Buffer::WatermarkFactorySharedPtr& watermark_factory)
    : DispatcherImpl(
          name, api, time_system,
          [](Dispatcher& dispatcher) {
            return std::make_unique<ScaledRangeTimerManagerImpl>(dispatcher);
          },
          watermark_factory) {}

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system,
                               const ScaledRangeTimerManagerFactory& scaled_timer_factory,
                               const Buffer::WatermarkFactorySharedPtr& watermark_factory)
    : DispatcherImpl(name, api.threadFactory(), api.timeSource(), api.fileSystem(), time_system,
                     scaled_timer_factory,
                     watermark_factory != nullptr
                         ? watermark_factory
                         : std::make_shared<Buffer::WatermarkBufferFactory>(
                               api.bootstrap().overload_manager().buffer_factory_config())) {}

DispatcherImpl::DispatcherImpl(const std::string& name, Thread::ThreadFactory& thread_factory,
                               TimeSource& time_source, Filesystem::Instance& file_system,
                               Event::TimeSystem& time_system,
                               const ScaledRangeTimerManagerFactory& scaled_timer_factory,
                               const Buffer::WatermarkFactorySharedPtr& watermark_factory)
    : name_(name), thread_factory_(thread_factory), time_source_(time_source),
      file_system_(file_system), buffer_factory_(watermark_factory),
      scheduler_(time_system.createScheduler(base_scheduler_, base_scheduler_)),
      thread_local_delete_cb_(
          base_scheduler_.createSchedulableCallback([this]() -> void { runThreadLocalDelete(); })),
      deferred_delete_cb_(base_scheduler_.createSchedulableCallback(
          [this]() -> void { clearDeferredDeleteList(); })),
      post_cb_(base_scheduler_.createSchedulableCallback([this]() -> void { runPostCallbacks(); })),
      current_to_delete_(&to_delete_1_), scaled_timer_manager_(scaled_timer_factory(*this)) {
  ASSERT(!name_.empty());
  FatalErrorHandler::registerFatalErrorHandler(*this);
  updateApproximateMonotonicTimeInternal();
  base_scheduler_.registerOnCheckCallback(
      std::bind(&DispatcherImpl::updateApproximateMonotonicTime, this));
}

DispatcherImpl::~DispatcherImpl() {
  ENVOY_LOG(debug, "destroying dispatcher {}", name_);
  FatalErrorHandler::removeFatalErrorHandler(*this);
  // shutdown() should be called before destruction to properly clean up all lists.
  if (!shutdown_called_) {
    ENVOY_LOG(warn,
              "Dispatcher {} destroyed without calling shutdown(). This may lead to "
              "non-deterministic cleanup order and memory leaks.",
              name_);
  }
}

void DispatcherImpl::registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                                      std::chrono::milliseconds min_touch_interval) {
  ASSERT(!watchdog_registration_, "Each dispatcher can have at most one registered watchdog.");
  watchdog_registration_ =
      std::make_unique<WatchdogRegistration>(watchdog, *scheduler_, min_touch_interval, *this);
}

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

  touchWatchdog();
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
  return std::make_unique<Network::ServerConnectionImpl>(*this, std::move(socket),
                                                         std::move(transport_socket), stream_info);
}

Network::ClientConnectionPtr DispatcherImpl::createClientConnection(
    Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  ASSERT(isThreadSafe());

  auto* factory = Config::Utility::getFactoryByName<Network::ClientConnectionFactory>(
      std::string(address->addressType()));

  // The target address is usually offered by EDS and the EDS api should reject the unsupported
  // address.
  // TODO(lambdai): Return a closed connection if the factory is not found. Note that the caller
  // expects a non-null connection as of today so we cannot gracefully handle unsupported address
  // type.
#if defined(__linux__)
  // For Linux, the source address' network namespace is relevant for client connections, since that
  // is where the netns would be specified.
  if (source_address && source_address->networkNamespace().has_value()) {
    auto f = [&]() -> Network::ClientConnectionPtr {
      return factory->createClientConnection(
          *this, address, source_address, std::move(transport_socket), options, transport_options);
    };
    auto result = Network::Utility::execInNetworkNamespace(
        std::move(f), source_address->networkNamespace()->c_str());
    if (!result.ok()) {
      ENVOY_LOG(error, "failed to create connection in network namespace {}: {}",
                source_address->networkNamespace().value(), result.status().ToString());
      return nullptr;
    }
    return *std::move(result);
  }
#endif

  return factory->createClientConnection(*this, address, source_address,
                                         std::move(transport_socket), options, transport_options);
}

FileEventPtr DispatcherImpl::createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                                             uint32_t events) {
  ASSERT(isThreadSafe());
  return FileEventPtr{new FileEventImpl(
      *this, fd,
      [this, cb](uint32_t events) {
        touchWatchdog();
        return cb(events);
      },
      trigger, events)};
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  ASSERT(isThreadSafe());
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this, file_system_)};
}

TimerPtr DispatcherImpl::createTimer(TimerCb cb) {
  ASSERT(isThreadSafe());
  return createTimerInternal(cb);
}

TimerPtr DispatcherImpl::createScaledTimer(ScaledTimerType timer_type, TimerCb cb) {
  ASSERT(isThreadSafe());
  return scaled_timer_manager_->createTimer(timer_type, std::move(cb));
}

TimerPtr DispatcherImpl::createScaledTimer(ScaledTimerMinimum minimum, TimerCb cb) {
  ASSERT(isThreadSafe());
  return scaled_timer_manager_->createTimer(minimum, std::move(cb));
}

Event::SchedulableCallbackPtr DispatcherImpl::createSchedulableCallback(std::function<void()> cb) {
  ASSERT(isThreadSafe());
  return base_scheduler_.createSchedulableCallback([this, cb]() {
    touchWatchdog();
    cb();
  });
}

TimerPtr DispatcherImpl::createTimerInternal(TimerCb cb) {
  return scheduler_->createTimer(
      [this, cb]() {
        touchWatchdog();
        cb();
      },
      *this);
}

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  ASSERT(isThreadSafe());
  if (to_delete != nullptr) {
    to_delete->deleteIsPending();
    current_to_delete_->emplace_back(std::move(to_delete));
    ENVOY_LOG(trace, "item added to deferred deletion list (size={})", current_to_delete_->size());
    if (current_to_delete_->size() == 1) {
      deferred_delete_cb_->scheduleCallbackCurrentIteration();
    }
  }
}

void DispatcherImpl::exit() { base_scheduler_.loopExit(); }

SignalEventPtr DispatcherImpl::listenForSignal(signal_t signal_num, SignalCb cb) {
  ASSERT(isThreadSafe());
  return SignalEventPtr{new SignalEventImpl(*this, signal_num, cb)};
}

void DispatcherImpl::post(PostCb callback) {
  bool do_post;
  {
    Thread::LockGuard lock(post_lock_);
    do_post = post_callbacks_.empty();
    post_callbacks_.push_back(std::move(callback));
  }

  if (do_post) {
    post_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::deleteInDispatcherThread(DispatcherThreadDeletableConstPtr deletable) {
  bool need_schedule;
  bool is_shutting_down;
  {
    Thread::LockGuard lock(thread_local_deletable_lock_);
    is_shutting_down = shutdown_called_;
    if (!is_shutting_down) {
      need_schedule = deletables_in_dispatcher_thread_.empty();
      deletables_in_dispatcher_thread_.emplace_back(std::move(deletable));
    }
    // If shutdown has been called, discard the item. This can happen when destructors of objects
    // being cleaned up during shutdown try to add more items. We silently discard them because
    // shutdown is already in progress and will complete cleanup.
  }

  if (!is_shutting_down && need_schedule) {
    thread_local_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::run(RunType type) {
  run_tid_ = thread_factory_.currentThreadId();
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

void DispatcherImpl::shutdown() {
  ASSERT(isThreadSafe());

  // Make shutdown() idempotent - if already called, just return. This handles cases where
  // shutdown() might be called multiple times (e.g., explicitly and from destructor).
  if (shutdown_called_) {
    ENVOY_LOG(debug, "Dispatcher {} shutdown() called multiple times, ignoring", name_);
    return;
  }
  shutdown_called_ = true;

  // Loop until all three lists are empty. Deleting objects from one list may add items to another
  // list. For example, deferred delete destructors may post callbacks, and post callbacks may
  // add deferred deletions. This ensures proper cleanup order and prevents dangling pointers.
  // See https://github.com/envoyproxy/envoy/issues/15072 for details.
  //
  // The iteration limit protects against infinite loops where cleanup code continuously creates
  // new work. While GoogleAsyncClient uses an unbounded loop, we use a high limit (1000) to catch
  // pathological cases while allowing deep legitimate dependency chains. If this limit is reached,
  // it likely indicates a bug in cleanup code that's creating an unbounded chain of work.
  constexpr int kMaxIterations = 1000; // Safety limit to catch infinite loops.
  int iteration = 0;
  size_t total_deferred_deleted = 0;
  size_t total_post_callbacks = 0;
  size_t total_thread_local_deleted = 0;

  bool has_pending_work = true;
  while (has_pending_work && iteration < kMaxIterations) {
    has_pending_work = false;

    // Clean deferred delete list.
    if (current_to_delete_->size() > 0) {
      total_deferred_deleted += current_to_delete_->size();
      clearDeferredDeleteList();
      has_pending_work = true;
    }

    // Discard post callbacks without running them. Post callbacks may reference objects that are
    // being destroyed during shutdown, so executing them would be unsafe and can cause crashes.
    // We only need to prevent new callbacks from being added (via shutdown_called_ check).
    {
      Thread::LockGuard lock(post_lock_);
      if (!post_callbacks_.empty()) {
        total_post_callbacks += post_callbacks_.size();
        post_callbacks_.clear();
        has_pending_work = true;
      }
    }

    // Clean thread local deletables.
    bool has_thread_local_deletables = false;
    {
      Thread::LockGuard lock(thread_local_deletable_lock_);
      has_thread_local_deletables = !deletables_in_dispatcher_thread_.empty();
      if (has_thread_local_deletables) {
        total_thread_local_deleted += deletables_in_dispatcher_thread_.size();
      }
    }
    if (has_thread_local_deletables) {
      runThreadLocalDelete();
      has_pending_work = true;
    }

    iteration++;
  }

  if (iteration >= kMaxIterations) {
    size_t remaining_post_callbacks = 0;
    size_t remaining_thread_local_deletables = 0;
    {
      Thread::LockGuard lock(post_lock_);
      remaining_post_callbacks = post_callbacks_.size();
    }
    {
      Thread::LockGuard lock(thread_local_deletable_lock_);
      remaining_thread_local_deletables = deletables_in_dispatcher_thread_.size();
    }
    ENVOY_LOG(critical,
              "Dispatcher {} shutdown reached maximum iterations ({}). This may indicate a cleanup "
              "loop. Remaining: {} deferred deletables, {} post callbacks, {} thread local "
              "deletables.",
              name_, kMaxIterations, current_to_delete_->size(), remaining_post_callbacks,
              remaining_thread_local_deletables);
  }

  ENVOY_LOG(debug,
            "Dispatcher {} shutdown completed in {} iterations. Cleaned up {} deferred deletables, "
            "{} post callbacks, {} thread local deletables.",
            name_, iteration, total_deferred_deleted, total_post_callbacks,
            total_thread_local_deleted);
}

void DispatcherImpl::updateApproximateMonotonicTime() { updateApproximateMonotonicTimeInternal(); }

void DispatcherImpl::updateApproximateMonotonicTimeInternal() {
  approximate_monotonic_time_ = time_source_.monotonicTime();
}

void DispatcherImpl::runThreadLocalDelete() {
  std::list<DispatcherThreadDeletableConstPtr> to_be_delete;
  {
    Thread::LockGuard lock(thread_local_deletable_lock_);
    to_be_delete = std::move(deletables_in_dispatcher_thread_);
    ASSERT(deletables_in_dispatcher_thread_.empty());
  }
  while (!to_be_delete.empty()) {
    // Touch the watchdog before deleting the objects to avoid spurious watchdog miss events when
    // executing complicated destruction.
    touchWatchdog();
    // Delete in FIFO order.
    to_be_delete.pop_front();
  }
}
void DispatcherImpl::runPostCallbacks() {
  // Clear the deferred delete list before running post callbacks to reduce non-determinism in
  // callback processing, and more easily detect if a scheduled post callback refers to one of the
  // objects that is being deferred deleted.
  clearDeferredDeleteList();

  std::list<PostCb> callbacks;
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
    touchWatchdog();
    // Run the callback.
    callbacks.front()();
    // Pop the front so that the destructor of the callback that just executed runs before the next
    // callback executes.
    callbacks.pop_front();
  }
}

void DispatcherImpl::onFatalError(std::ostream& os) const {
  // Dump the state of the tracked objects in the dispatcher if thread safe. This generally
  // results in dumping the active state only for the thread which caused the fatal error.
  if (isThreadSafe()) {
    for (auto iter = tracked_object_stack_.rbegin(); iter != tracked_object_stack_.rend(); ++iter) {
      (*iter)->dumpState(os);
    }
  }
}

void DispatcherImpl::runFatalActionsOnTrackedObject(
    const FatalAction::FatalActionPtrList& actions) const {
  // Only run if this is the dispatcher of the current thread and
  // DispatcherImpl::Run has been called.
  if (run_tid_.isEmpty() || (run_tid_ != thread_factory_.currentThreadId())) {
    return;
  }

  for (const auto& action : actions) {
    action->run(tracked_object_stack_);
  }
}

void DispatcherImpl::touchWatchdog() {
  if (watchdog_registration_) {
    watchdog_registration_->touchWatchdog();
  }
}

void DispatcherImpl::pushTrackedObject(const ScopeTrackedObject* object) {
  ASSERT(isThreadSafe());
  ASSERT(object != nullptr);
  tracked_object_stack_.push_back(object);
  ASSERT(tracked_object_stack_.size() <= ExpectedMaxTrackedObjectStackDepth);
}

void DispatcherImpl::popTrackedObject(const ScopeTrackedObject* expected_object) {
  ASSERT(isThreadSafe());
  ASSERT(expected_object != nullptr);
  RELEASE_ASSERT(!tracked_object_stack_.empty(), "Tracked Object Stack is empty, nothing to pop!");

  const ScopeTrackedObject* top = tracked_object_stack_.back();
  tracked_object_stack_.pop_back();
  ASSERT(top == expected_object,
         "Popped the top of the tracked object stack, but it wasn't the expected object!");
}

} // namespace Event
} // namespace Envoy

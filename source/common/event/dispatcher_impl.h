#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection_handler.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/event/libevent.h"
#include "source/common/event/libevent_scheduler.h"
#include "source/common/signal/fatal_error_handler.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Event {

// The tracked object stack likely won't grow larger than this initial
// reservation; this should make appends constant time since the stack
// shouldn't have to grow larger.
inline constexpr size_t ExpectedMaxTrackedObjectStackDepth = 10;

/**
 * libevent implementation of Event::Dispatcher.
 */
class DispatcherImpl : Logger::Loggable<Logger::Id::main>,
                       public Dispatcher,
                       public FatalErrorHandlerInterface {
public:
  DispatcherImpl(const std::string& name, Api::Api& api, Event::TimeSystem& time_system);
  DispatcherImpl(const std::string& name, Api::Api& api, Event::TimeSystem& time_systems,
                 const Buffer::WatermarkFactorySharedPtr& watermark_factory);
  DispatcherImpl(const std::string& name, Api::Api& api, Event::TimeSystem& time_system,
                 const ScaledRangeTimerManagerFactory& scaled_timer_factory,
                 const Buffer::WatermarkFactorySharedPtr& watermark_factory);
  DispatcherImpl(const std::string& name, Thread::ThreadFactory& thread_factory,
                 TimeSource& time_source, Filesystem::Instance& file_system,
                 Event::TimeSystem& time_system,
                 const ScaledRangeTimerManagerFactory& scaled_timer_factory,
                 const Buffer::WatermarkFactorySharedPtr& watermark_factory);
  ~DispatcherImpl() override;

  /**
   * @return event_base& the libevent base.
   */
  event_base& base() { return base_scheduler_.base(); }

  // Event::Dispatcher
  const std::string& name() override { return name_; }
  void registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                        std::chrono::milliseconds min_touch_interval) override;
  TimeSource& timeSource() override { return time_source_; }
  void initializeStats(Stats::Scope& scope, const absl::optional<std::string>& prefix) override;
  void clearDeferredDeleteList() override;
  Network::ServerConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket,
                         StreamInfo::StreamInfo& stream_info) override;
  Network::ClientConnectionPtr createClientConnection(
      Network::Address::InstanceConstSharedPtr address,
      Network::Address::InstanceConstSharedPtr source_address,
      Network::TransportSocketPtr&& transport_socket,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_options) override;
  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override;
  Filesystem::WatcherPtr createFilesystemWatcher() override;
  TimerPtr createTimer(TimerCb cb) override;
  TimerPtr createScaledTimer(ScaledTimerType timer_type, TimerCb cb) override;
  TimerPtr createScaledTimer(ScaledTimerMinimum minimum, TimerCb cb) override;

  Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override;
  void deferredDelete(DeferredDeletablePtr&& to_delete) override;
  void exit() override;
  SignalEventPtr listenForSignal(signal_t signal_num, SignalCb cb) override;
  void post(PostCb callback) override;
  void deleteInDispatcherThread(DispatcherThreadDeletableConstPtr deletable) override;
  void run(RunType type) override;
  Buffer::WatermarkFactory& getWatermarkFactory() override { return *buffer_factory_; }
  void pushTrackedObject(const ScopeTrackedObject* object) override;
  void popTrackedObject(const ScopeTrackedObject* expected_object) override;
  bool trackedObjectStackIsEmpty() const override { return tracked_object_stack_.empty(); }
  MonotonicTime approximateMonotonicTime() const override;
  void updateApproximateMonotonicTime() override;
  void shutdown() override;

  // FatalErrorInterface
  void onFatalError(std::ostream& os) const override;
  void
  runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const override;

private:
  // Holds a reference to the watchdog registered with this dispatcher and the timer used to ensure
  // that the dog is touched periodically.
  class WatchdogRegistration {
  public:
    WatchdogRegistration(const Server::WatchDogSharedPtr& watchdog, Scheduler& scheduler,
                         std::chrono::milliseconds timer_interval, Dispatcher& dispatcher)
        : watchdog_(watchdog), timer_interval_(timer_interval) {
      touch_timer_ = scheduler.createTimer(
          [this]() -> void {
            watchdog_->touch();
            touch_timer_->enableTimer(timer_interval_);
          },
          dispatcher);
      touch_timer_->enableTimer(timer_interval_);
    }

    void touchWatchdog() { watchdog_->touch(); }

  private:
    Server::WatchDogSharedPtr watchdog_;
    const std::chrono::milliseconds timer_interval_;
    TimerPtr touch_timer_;
  };
  using WatchdogRegistrationPtr = std::unique_ptr<WatchdogRegistration>;

  TimerPtr createTimerInternal(TimerCb cb);
  void updateApproximateMonotonicTimeInternal();
  void runPostCallbacks();
  void runThreadLocalDelete();

  // Helper used to touch the watchdog after most schedulable, fd, and timer callbacks.
  void touchWatchdog();

  // Validate that an operation is thread safe, i.e. it's invoked on the same thread that the
  // dispatcher run loop is executing on. We allow run_tid_ to be empty for tests where we don't
  // invoke run().
  bool isThreadSafe() const override {
    return run_tid_.isEmpty() || run_tid_ == thread_factory_.currentThreadId();
  }

  const std::string name_;
  Thread::ThreadFactory& thread_factory_;
  TimeSource& time_source_;
  Filesystem::Instance& file_system_;
  std::string stats_prefix_;
  DispatcherStatsPtr stats_;
  Thread::ThreadId run_tid_;
  Buffer::WatermarkFactorySharedPtr buffer_factory_;
  LibeventScheduler base_scheduler_;
  SchedulerPtr scheduler_;

  SchedulableCallbackPtr thread_local_delete_cb_;
  Thread::MutexBasicLockable thread_local_deletable_lock_;
  // `deletables_in_dispatcher_thread` must be destroyed last to allow other callbacks populate.
  std::list<DispatcherThreadDeletableConstPtr>
      deletables_in_dispatcher_thread_ ABSL_GUARDED_BY(thread_local_deletable_lock_);
  bool shutdown_called_{false};

  SchedulableCallbackPtr deferred_delete_cb_;

  SchedulableCallbackPtr post_cb_;
  Thread::MutexBasicLockable post_lock_;
  std::list<PostCb> post_callbacks_ ABSL_GUARDED_BY(post_lock_);

  std::vector<DeferredDeletablePtr> to_delete_1_;
  std::vector<DeferredDeletablePtr> to_delete_2_;
  std::vector<DeferredDeletablePtr>* current_to_delete_;

  absl::InlinedVector<const ScopeTrackedObject*, ExpectedMaxTrackedObjectStackDepth>
      tracked_object_stack_;
  bool deferred_deleting_{};
  MonotonicTime approximate_monotonic_time_;
  WatchdogRegistrationPtr watchdog_registration_;
  const ScaledRangeTimerManagerPtr scaled_timer_manager_;
};

} // namespace Event
} // namespace Envoy

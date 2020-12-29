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

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/event/libevent_scheduler.h"
#include "common/signal/fatal_error_handler.h"

namespace Envoy {
namespace Event {

/**
 * Libevent-based implementation of the DispatcherBase abstraction. This is usually wrapped by
 * DispatcherImpl below.
 */
class DispatcherImplBase final : public DispatcherBase {
public:
  DispatcherImplBase(Api::Api& api, TimeSystem& time_system);

  // DispatcherBase impl
  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override;
  TimerPtr createTimer(TimerCb cb) override;
  SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override;
  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override;
  bool isThreadSafe() const override {
    return run_tid_.isEmpty() || run_tid_ == api_.threadFactory().currentThreadId();
  }
  MonotonicTime approximateMonotonicTime() const override { return approximate_monotonic_time_; }

  /**
   * @return event_base& the libevent base.
   */
  event_base& base() { return base_scheduler_.base(); }
  /**
   * @return Api::Api& the Api object used by the DispatcherBase.
   */
  Api::Api& api() { return api_; }
  /**
   * @return Thread::ThreadId the thread ID last set by saveTid().
   */
  Thread::ThreadId run_tid() const { return run_tid_; }
  /**
   * Saves the ID of the calling thread for later retrieval with run_tid().
   */
  void saveTid() { run_tid_ = api_.threadFactory().currentThreadId(); }
  /**
   * Creates a schedulable callback that doesn't touch the watchdog before running.
   */
  SchedulableCallbackPtr createRawSchedulableCallback(std::function<void()> cb);

  /**
   * Registers the given watchdog with the base dispatcher.
   */
  void registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                        std::chrono::milliseconds min_touch_interval);

  /**
   * Helper used to touch the watchdog after most schedulable, fd, and timer callbacks.
   */
  void touchWatchdog();

  // Utility functions used to implement the full Dispatcher interface.
  void registerOnPrepareCallback(LibeventScheduler::OnPrepareCallback&& callback) {
    base_scheduler_.registerOnPrepareCallback(std::move(callback));
  }
  void initializeStats(DispatcherStats* stats) { base_scheduler_.initializeStats(stats); }
  void run(Dispatcher::RunType type) { base_scheduler_.run(type); }
  void loopExit() { base_scheduler_.loopExit(); }
  void runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const;
  void onFatalError(std::ostream& os) const;
  void updateApproximateMonotonicTime() {
    approximate_monotonic_time_ = api_.timeSource().monotonicTime();
  }

private:
  // Holds a reference to the watchdog registered with this dispatcher and the timer used to ensure
  // that the dog is touched periodically.
  class WatchdogRegistration {
  public:
    WatchdogRegistration(const Server::WatchDogSharedPtr& watchdog, Scheduler& scheduler,
                         std::chrono::milliseconds timer_interval, DispatcherBase& dispatcher);

    void touchWatchdog() { watchdog_->touch(); }

  private:
    Server::WatchDogSharedPtr watchdog_;
    const std::chrono::milliseconds timer_interval_;
    TimerPtr touch_timer_;
  };
  using WatchdogRegistrationPtr = std::unique_ptr<WatchdogRegistration>;

  LibeventScheduler base_scheduler_;
  WatchdogRegistrationPtr watchdog_registration_;
  const ScopeTrackedObject* current_object_{};
  Api::Api& api_;
  SchedulerPtr scheduler_;
  Thread::ThreadId run_tid_;
  MonotonicTime approximate_monotonic_time_;
};

/**
 * libevent implementation of Event::Dispatcher.
 */
class DispatcherImpl : public Logger::Loggable<Logger::Id::main>,
                       public Dispatcher,
                       public FatalErrorHandlerInterface {
public:
  DispatcherImpl(const std::string& name, Api::Api& api, Event::TimeSystem& time_system,
                 const Buffer::WatermarkFactorySharedPtr& factory = nullptr);
  ~DispatcherImpl() override;

  // DispatcherBase impl
  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override {
    return base_.createFileEvent(fd, std::move(cb), trigger, events);
  }
  TimerPtr createTimer(TimerCb cb) override { return base_.createTimer(std::move(cb)); }
  SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override {
    return base_.createSchedulableCallback(std::move(cb));
  }
  const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) override {
    return base_.setTrackedObject(object);
  }
  bool isThreadSafe() const override { return base_.isThreadSafe(); }
  MonotonicTime approximateMonotonicTime() const override {
    return base_.approximateMonotonicTime();
  }
  // Event::Dispatcher
  const std::string& name() override { return name_; }
  void registerWatchdog(const Server::WatchDogSharedPtr& watchdog,
                        std::chrono::milliseconds min_touch_interval) override;
  TimeSource& timeSource() override { return base_.api().timeSource(); }
  void initializeStats(Stats::Scope& scope, const absl::optional<std::string>& prefix) override;
  void clearDeferredDeleteList() override;
  Network::ServerConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket,
                         StreamInfo::StreamInfo& stream_info) override;
  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  Network::DnsResolverSharedPtr
  createDnsResolver(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
                    const bool use_tcp_for_dns_lookups) override;
  Filesystem::WatcherPtr createFilesystemWatcher() override;
  Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                      Network::TcpListenerCallbacks& cb, bool bind_to_port,
                                      uint32_t backlog_size) override;
  Network::UdpListenerPtr createUdpListener(Network::SocketSharedPtr socket,
                                            Network::UdpListenerCallbacks& cb) override;
  void deferredDelete(DeferredDeletablePtr&& to_delete) override;
  void exit() override { base_.loopExit(); }
  SignalEventPtr listenForSignal(signal_t signal_num, SignalCb cb) override;
  void post(std::function<void()> callback) override;
  void run(RunType type) override;
  Buffer::WatermarkFactory& getWatermarkFactory() override { return *buffer_factory_; }
  void updateApproximateMonotonicTime() override { base_.updateApproximateMonotonicTime(); }

  // FatalErrorInterface
  void onFatalError(std::ostream& os) const override { base_.onFatalError(os); }

  event_base& base() { return base_.base(); }

  void
  runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const override;

private:
  TimerPtr createTimerInternal(TimerCb cb);
  void runPostCallbacks();

  DispatcherImplBase base_;
  const std::string name_;
  std::string stats_prefix_;
  DispatcherStatsPtr stats_;
  Buffer::WatermarkFactorySharedPtr buffer_factory_;
  SchedulableCallbackPtr deferred_delete_cb_;
  SchedulableCallbackPtr post_cb_;
  std::vector<DeferredDeletablePtr> to_delete_1_;
  std::vector<DeferredDeletablePtr> to_delete_2_;
  std::vector<DeferredDeletablePtr>* current_to_delete_;
  Thread::MutexBasicLockable post_lock_;
  std::list<std::function<void()>> post_callbacks_ ABSL_GUARDED_BY(post_lock_);
  bool deferred_deleting_{};
};

} // namespace Event
} // namespace Envoy

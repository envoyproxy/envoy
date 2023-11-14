#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/common/scope_tracker.h"
#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/signal.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/dns.h"
#include "envoy/network/listener.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"

#include "source/common/common/scope_tracker.h"

#include "test/mocks/buffer/mocks.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Event {

class MockDispatcher : public Dispatcher {
public:
  MockDispatcher();
  MockDispatcher(const std::string& name);
  ~MockDispatcher() override;

  // Dispatcher
  const std::string& name() override { return name_; }
  TimeSource& timeSource() override { return *time_system_; }
  GlobalTimeSystem& globalTimeSystem() {
    return *(dynamic_cast<GlobalTimeSystem*>(time_system_.get()));
  }
  Network::ServerConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket,
                         StreamInfo::StreamInfo&) override {
    // The caller expects both the socket and the transport socket to be moved.
    socket.reset();
    transport_socket.reset();
    return Network::ServerConnectionPtr{createServerConnection_()};
  }

  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options,
                         const Network::TransportSocketOptionsConstSharedPtr&) override {
    return Network::ClientConnectionPtr{
        createClientConnection_(address, source_address, transport_socket, options)};
  }

  FileEventPtr createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override {
    return FileEventPtr{createFileEvent_(fd, cb, trigger, events)};
  }

  Filesystem::WatcherPtr createFilesystemWatcher() override {
    return Filesystem::WatcherPtr{createFilesystemWatcher_()};
  }

  Event::TimerPtr createTimer(Event::TimerCb cb) override {
    auto timer = Event::TimerPtr{createTimer_(cb)};
    // Assert that the timer is not null to avoid confusing test failures down the line.
    ASSERT(timer != nullptr);
    return timer;
  }

  Event::TimerPtr createScaledTimer(ScaledTimerMinimum minimum, Event::TimerCb cb) override {
    auto timer = Event::TimerPtr{createScaledTimer_(minimum, cb)};
    // Assert that the timer is not null to avoid confusing test failures down the line.
    ASSERT(timer != nullptr);
    return timer;
  }

  Event::TimerPtr createScaledTimer(ScaledTimerType timer_type, Event::TimerCb cb) override {
    auto timer = Event::TimerPtr{createScaledTypedTimer_(timer_type, cb)};
    // Assert that the timer is not null to avoid confusing test failures down the line.
    ASSERT(timer != nullptr);
    return timer;
  }

  Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override {
    auto schedulable_cb = Event::SchedulableCallbackPtr{createSchedulableCallback_(cb)};
    if (!allow_null_callback_) {
      // Assert that schedulable_cb is not null to avoid confusing test failures down the line.
      ASSERT(schedulable_cb != nullptr);
    }
    return schedulable_cb;
  }

  void deferredDelete(DeferredDeletablePtr&& to_delete) override {
    if (to_delete) {
      to_delete->deleteIsPending();
    }
    deferredDelete_(to_delete.get());
    if (to_delete) {
      to_delete_.push_back(std::move(to_delete));
    }
  }

  SignalEventPtr listenForSignal(signal_t signal_num, SignalCb cb) override {
    return SignalEventPtr{listenForSignal_(signal_num, cb)};
  }

  // Event::Dispatcher
  MOCK_METHOD(void, registerWatchdog,
              (const Server::WatchDogSharedPtr&, std::chrono::milliseconds));
  MOCK_METHOD(void, initializeStats, (Stats::Scope&, const absl::optional<std::string>&));
  MOCK_METHOD(void, clearDeferredDeleteList, ());
  MOCK_METHOD(Network::ServerConnection*, createServerConnection_, ());
  MOCK_METHOD(Network::ClientConnection*, createClientConnection_,
              (Network::Address::InstanceConstSharedPtr address,
               Network::Address::InstanceConstSharedPtr source_address,
               Network::TransportSocketPtr& transport_socket,
               const Network::ConnectionSocket::OptionsSharedPtr& options));
  MOCK_METHOD(FileEvent*, createFileEvent_,
              (os_fd_t fd, FileReadyCb cb, FileTriggerType trigger, uint32_t events));
  MOCK_METHOD(Filesystem::Watcher*, createFilesystemWatcher_, ());
  MOCK_METHOD(Timer*, createTimer_, (Event::TimerCb cb));
  MOCK_METHOD(Timer*, createScaledTimer_, (ScaledTimerMinimum minimum, Event::TimerCb cb));
  MOCK_METHOD(Timer*, createScaledTypedTimer_, (ScaledTimerType timer_type, Event::TimerCb cb));
  MOCK_METHOD(SchedulableCallback*, createSchedulableCallback_, (std::function<void()> cb));
  MOCK_METHOD(void, deferredDelete_, (DeferredDeletable * to_delete));
  MOCK_METHOD(void, exit, ());
  MOCK_METHOD(SignalEvent*, listenForSignal_, (signal_t signal_num, SignalCb cb));
  MOCK_METHOD(void, post, (PostCb callback));
  MOCK_METHOD(void, deleteInDispatcherThread, (DispatcherThreadDeletableConstPtr deletable));
  MOCK_METHOD(void, run, (RunType type));
  MOCK_METHOD(void, pushTrackedObject, (const ScopeTrackedObject* object));
  MOCK_METHOD(void, popTrackedObject, (const ScopeTrackedObject* expected_object));
  MOCK_METHOD(bool, trackedObjectStackIsEmpty, (), (const));
  MOCK_METHOD(bool, isThreadSafe, (), (const));
  Buffer::WatermarkFactory& getWatermarkFactory() override { return buffer_factory_; }
  MOCK_METHOD(Thread::ThreadId, getCurrentThreadId, ());
  MOCK_METHOD(MonotonicTime, approximateMonotonicTime, (), (const));
  MOCK_METHOD(void, updateApproximateMonotonicTime, ());
  MOCK_METHOD(void, shutdown, ());

  std::unique_ptr<TimeSource> time_system_;
  std::list<DeferredDeletablePtr> to_delete_;
  testing::NiceMock<MockBufferFactory> buffer_factory_;
  bool allow_null_callback_{};

private:
  const std::string name_;
};

class MockTimer : public Timer {
public:
  MockTimer();

  // Ownership of each MockTimer instance is transferred to the (caller of) dispatcher's
  // createTimer_(), so to avoid destructing it twice, the MockTimer must have been dynamically
  // allocated and must not be deleted by it's creator.
  MockTimer(MockDispatcher* dispatcher);
  ~MockTimer() override;

  void invokeCallback() {
    EXPECT_TRUE(enabled_);
    enabled_ = false;
    if (scope_ == nullptr) {
      callback_();
      return;
    }
    ScopeTrackerScopeState scope(scope_, *dispatcher_);
    scope_ = nullptr;
    callback_();
  }

  // Timer
  MOCK_METHOD(void, disableTimer, ());
  MOCK_METHOD(void, enableTimer, (std::chrono::milliseconds, const ScopeTrackedObject* scope));
  MOCK_METHOD(void, enableHRTimer, (std::chrono::microseconds, const ScopeTrackedObject* scope));
  MOCK_METHOD(bool, enabled, ());

  MockDispatcher* dispatcher_{};
  const ScopeTrackedObject* scope_{};
  bool enabled_{};

  Event::TimerCb callback_;

  // If not nullptr, will be set on dtor. This can help to verify that the timer was destroyed.
  bool* timer_destroyed_{};
};

class MockScaledRangeTimerManager : public ScaledRangeTimerManager {
public:
  TimerPtr createTimer(ScaledTimerMinimum minimum, TimerCb callback) override {
    return TimerPtr{createTimer_(minimum, std::move(callback))};
  }
  TimerPtr createTimer(ScaledTimerType timer_type, TimerCb callback) override {
    return TimerPtr{createTypedTimer_(timer_type, std::move(callback))};
  }
  MOCK_METHOD(Timer*, createTimer_, (ScaledTimerMinimum, TimerCb));
  MOCK_METHOD(Timer*, createTypedTimer_, (ScaledTimerType, TimerCb));
  MOCK_METHOD(void, setScaleFactor, (UnitFloat), (override));
};

class MockSchedulableCallback : public SchedulableCallback {
public:
  MockSchedulableCallback(MockDispatcher* dispatcher,
                          testing::MockFunction<void()>* destroy_cb = nullptr);
  MockSchedulableCallback(MockDispatcher* dispatcher, std::function<void()> callback,
                          testing::MockFunction<void()>* destroy_cb = nullptr);
  ~MockSchedulableCallback() override;

  void invokeCallback() {
    EXPECT_TRUE(enabled_);
    enabled_ = false;
    callback_();
  }

  // SchedulableCallback
  MOCK_METHOD(void, scheduleCallbackCurrentIteration, ());
  MOCK_METHOD(void, scheduleCallbackNextIteration, ());
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(bool, enabled, ());

  MockDispatcher* dispatcher_{};
  bool enabled_{};

private:
  std::function<void()> callback_;
  testing::MockFunction<void()>* destroy_cb_{nullptr};
};

class MockSignalEvent : public SignalEvent {
public:
  MockSignalEvent(MockDispatcher* dispatcher);
  ~MockSignalEvent() override;

  SignalCb callback_;
};

class MockFileEvent : public FileEvent {
public:
  MockFileEvent();
  ~MockFileEvent() override;

  MOCK_METHOD(void, activate, (uint32_t events));
  MOCK_METHOD(void, setEnabled, (uint32_t events));
  MOCK_METHOD(void, registerEventIfEmulatedEdge, (uint32_t event));
  MOCK_METHOD(void, unregisterEventIfEmulatedEdge, (uint32_t event));
};

} // namespace Event
} // namespace Envoy

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/dns.h"
#include "envoy/network/listener.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"

#include "common/common/scope_tracker.h"

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
  TimeSource& timeSource() override { return time_system_; }
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
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override {
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

  Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                      Network::TcpListenerCallbacks& cb, bool bind_to_port,
                                      uint32_t backlog_size) override {
    return Network::ListenerPtr{createListener_(std::move(socket), cb, bind_to_port, backlog_size)};
  }

  Network::UdpListenerPtr createUdpListener(Network::SocketSharedPtr socket,
                                            Network::UdpListenerCallbacks& cb) override {
    return Network::UdpListenerPtr{createUdpListener_(socket, cb)};
  }

  Event::TimerPtr createTimer(Event::TimerCb cb) override {
    auto timer = Event::TimerPtr{createTimer_(cb)};
    // Assert that the timer is not null to avoid confusing test failures down the line.
    ASSERT(timer != nullptr);
    return timer;
  }

  Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb) override {
    auto schedulable_cb = Event::SchedulableCallbackPtr{createSchedulableCallback_(cb)};
    // Assert that schedulable_cb is not null to avoid confusing test failures down the line.
    ASSERT(schedulable_cb != nullptr);
    return schedulable_cb;
  }

  void deferredDelete(DeferredDeletablePtr&& to_delete) override {
    deferredDelete_(to_delete.get());
    if (to_delete) {
      to_delete_.push_back(std::move(to_delete));
    }
  }

  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override {
    return SignalEventPtr{listenForSignal_(signal_num, cb)};
  }

  // Event::Dispatcher
  MOCK_METHOD(void, initializeStats, (Stats::Scope&, const absl::optional<std::string>&));
  MOCK_METHOD(void, clearDeferredDeleteList, ());
  MOCK_METHOD(Network::ServerConnection*, createServerConnection_, ());
  MOCK_METHOD(Network::ClientConnection*, createClientConnection_,
              (Network::Address::InstanceConstSharedPtr address,
               Network::Address::InstanceConstSharedPtr source_address,
               Network::TransportSocketPtr& transport_socket,
               const Network::ConnectionSocket::OptionsSharedPtr& options));
  MOCK_METHOD(Network::DnsResolverSharedPtr, createDnsResolver,
              (const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
               const bool use_tcp_for_dns_lookups));
  MOCK_METHOD(FileEvent*, createFileEvent_,
              (os_fd_t fd, FileReadyCb cb, FileTriggerType trigger, uint32_t events));
  MOCK_METHOD(Filesystem::Watcher*, createFilesystemWatcher_, ());
  MOCK_METHOD(Network::Listener*, createListener_,
              (Network::SocketSharedPtr && socket, Network::TcpListenerCallbacks& cb,
               bool bind_to_port, uint32_t backlog_size));
  MOCK_METHOD(Network::UdpListener*, createUdpListener_,
              (Network::SocketSharedPtr socket, Network::UdpListenerCallbacks& cb));
  MOCK_METHOD(Timer*, createTimer_, (Event::TimerCb cb));
  MOCK_METHOD(SchedulableCallback*, createSchedulableCallback_, (std::function<void()> cb));
  MOCK_METHOD(void, deferredDelete_, (DeferredDeletable * to_delete));
  MOCK_METHOD(void, exit, ());
  MOCK_METHOD(SignalEvent*, listenForSignal_, (int signal_num, SignalCb cb));
  MOCK_METHOD(void, post, (std::function<void()> callback));
  MOCK_METHOD(void, run, (RunType type));
  MOCK_METHOD(const ScopeTrackedObject*, setTrackedObject, (const ScopeTrackedObject* object));
  MOCK_METHOD(bool, isThreadSafe, (), (const));
  Buffer::WatermarkFactory& getWatermarkFactory() override { return buffer_factory_; }
  MOCK_METHOD(Thread::ThreadId, getCurrentThreadId, ());
  MOCK_METHOD(MonotonicTime, approximateMonotonicTime, (), (const));
  MOCK_METHOD(void, updateApproximateMonotonicTime, ());

  GlobalTimeSystem time_system_;
  std::list<DeferredDeletablePtr> to_delete_;
  MockBufferFactory buffer_factory_;

private:
  const std::string name_;
};

class MockTimer : public Timer {
public:
  MockTimer();
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

class MockSchedulableCallback : public SchedulableCallback {
public:
  MockSchedulableCallback(MockDispatcher* dispatcher);
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
};

} // namespace Event
} // namespace Envoy

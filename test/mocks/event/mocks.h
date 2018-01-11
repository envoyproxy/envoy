#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/dns.h"
#include "envoy/network/listener.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"

#include "test/mocks/buffer/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Event {

class MockDispatcher : public Dispatcher {
public:
  MockDispatcher();
  ~MockDispatcher();

  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket) override {
    return Network::ClientConnectionPtr{
        createClientConnection_(address, source_address, transport_socket)};
  }

  FileEventPtr createFileEvent(int fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override {
    return FileEventPtr{createFileEvent_(fd, cb, trigger, events)};
  }

  Filesystem::WatcherPtr createFilesystemWatcher() override {
    return Filesystem::WatcherPtr{createFilesystemWatcher_()};
  }

  Network::ListenerPtr createListener(Network::ConnectionHandler& conn_handler,
                                      Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                      Stats::Scope& scope,
                                      const Network::ListenerOptions& listener_options) override {
    return Network::ListenerPtr{createListener_(conn_handler, socket, cb, scope, listener_options)};
  }

  Network::ListenerPtr
  createSslListener(Network::ConnectionHandler& conn_handler, Ssl::ServerContext& ssl_ctx,
                    Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                    Stats::Scope& scope,
                    const Network::ListenerOptions& listener_options) override {
    return Network::ListenerPtr{
        createSslListener_(conn_handler, ssl_ctx, socket, cb, scope, listener_options)};
  }

  TimerPtr createTimer(TimerCb cb) override { return TimerPtr{createTimer_(cb)}; }

  void deferredDelete(DeferredDeletablePtr&& to_delete) override {
    deferredDelete_(to_delete);
    if (to_delete) {
      to_delete_.push_back(std::move(to_delete));
    }
  }

  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override {
    return SignalEventPtr{listenForSignal_(signal_num, cb)};
  }

  // Event::Dispatcher
  MOCK_METHOD0(clearDeferredDeleteList, void());
  MOCK_METHOD3(createClientConnection_,
               Network::ClientConnection*(Network::Address::InstanceConstSharedPtr address,
                                          Network::Address::InstanceConstSharedPtr source_address,
                                          Network::TransportSocketPtr& transport_socket));
  MOCK_METHOD1(createDnsResolver,
               Network::DnsResolverSharedPtr(
                   const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers));
  MOCK_METHOD4(createFileEvent_,
               FileEvent*(int fd, FileReadyCb cb, FileTriggerType trigger, uint32_t events));
  MOCK_METHOD0(createFilesystemWatcher_, Filesystem::Watcher*());
  MOCK_METHOD5(createListener_,
               Network::Listener*(Network::ConnectionHandler& conn_handler,
                                  Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                  Stats::Scope& scope,
                                  const Network::ListenerOptions& listener_options));
  MOCK_METHOD6(createSslListener_,
               Network::Listener*(Network::ConnectionHandler& conn_handler,
                                  Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                  Network::ListenerCallbacks& cb, Stats::Scope& scope,
                                  const Network::ListenerOptions& listener_options));
  MOCK_METHOD1(createTimer_, Timer*(TimerCb cb));
  MOCK_METHOD1(deferredDelete_, void(DeferredDeletablePtr& to_delete));
  MOCK_METHOD0(exit, void());
  MOCK_METHOD2(listenForSignal_, SignalEvent*(int signal_num, SignalCb cb));
  MOCK_METHOD1(post, void(std::function<void()> callback));
  MOCK_METHOD1(run, void(RunType type));
  Buffer::WatermarkFactory& getWatermarkFactory() override { return buffer_factory_; }

  std::list<DeferredDeletablePtr> to_delete_;
  MockBufferFactory buffer_factory_;
};

class MockTimer : public Timer {
public:
  MockTimer();
  MockTimer(MockDispatcher* dispatcher);
  ~MockTimer();

  // Event::Timer
  MOCK_METHOD0(disableTimer, void());
  MOCK_METHOD1(enableTimer, void(const std::chrono::milliseconds&));

  TimerCb callback_;
};

class MockSignalEvent : public SignalEvent {
public:
  MockSignalEvent(MockDispatcher* dispatcher);
  ~MockSignalEvent();

  SignalCb callback_;
};

class MockFileEvent : public FileEvent {
public:
  MockFileEvent();
  ~MockFileEvent();

  MOCK_METHOD1(activate, void(uint32_t events));
  MOCK_METHOD1(setEnabled, void(uint32_t events));
};

} // namespace Event
} // namespace Envoy

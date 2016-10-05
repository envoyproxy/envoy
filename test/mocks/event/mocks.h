#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/dns.h"
#include "envoy/network/listener.h"
#include "envoy/ssl/context.h"

namespace Event {

class MockDispatcher : public Dispatcher {
public:
  MockDispatcher();
  ~MockDispatcher();

  Network::ClientConnectionPtr createClientConnection(const std::string& url) override {
    return Network::ClientConnectionPtr{createClientConnection_(url)};
  }

  Network::ClientConnectionPtr createSslClientConnection(Ssl::ClientContext& ssl_ctx,
                                                         const std::string& url) override {
    return Network::ClientConnectionPtr{createSslClientConnection_(ssl_ctx, url)};
  }

  Network::DnsResolverPtr createDnsResolver() override {
    return Network::DnsResolverPtr{createDnsResolver_()};
  }

  FileEventPtr createFileEvent(int fd, FileReadyCb read_cb, FileReadyCb write_cb) override {
    return FileEventPtr{createFileEvent_(fd, read_cb, write_cb)};
  }

  Filesystem::WatcherPtr createFilesystemWatcher() override {
    return Filesystem::WatcherPtr{createFilesystemWatcher_()};
  }

  Network::ListenerPtr createListener(Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                      Stats::Store& stats_store, bool use_proxy_proto) override {
    return Network::ListenerPtr{createListener_(socket, cb, stats_store, use_proxy_proto)};
  }

  Network::ListenerPtr createSslListener(Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                         Network::ListenerCallbacks& cb, Stats::Store& stats_store,
                                         bool use_proxy_proto) override {
    return Network::ListenerPtr{
        createSslListener_(ssl_ctx, socket, cb, stats_store, use_proxy_proto)};
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
  MOCK_METHOD1(createClientConnection_, Network::ClientConnection*(const std::string& url));
  MOCK_METHOD2(createSslClientConnection_,
               Network::ClientConnection*(Ssl::ClientContext& ssl_ctx, const std::string& url));
  MOCK_METHOD0(createDnsResolver_, Network::DnsResolver*());
  MOCK_METHOD3(createFileEvent_, FileEvent*(int fd, FileReadyCb read_cb, FileReadyCb write_cb));
  MOCK_METHOD0(createFilesystemWatcher_, Filesystem::Watcher*());
  MOCK_METHOD4(createListener_,
               Network::Listener*(Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                  Stats::Store& stats_store, bool use_proxy_proto));
  MOCK_METHOD5(createSslListener_,
               Network::Listener*(Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                  Network::ListenerCallbacks& cb, Stats::Store& stats_store,
                                  bool use_proxy_proto));
  MOCK_METHOD1(createTimer_, Timer*(TimerCb cb));
  MOCK_METHOD1(deferredDelete_, void(DeferredDeletablePtr& to_delete));
  MOCK_METHOD0(exit, void());
  MOCK_METHOD2(listenForSignal_, SignalEvent*(int signal_num, SignalCb cb));
  MOCK_METHOD1(post, void(std::function<void()> callback));
  MOCK_METHOD1(run, void(RunType type));

private:
  std::list<DeferredDeletablePtr> to_delete_;
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

} // Event

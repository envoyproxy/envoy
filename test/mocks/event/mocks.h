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

  FileEventPtr createFileEvent(int fd, FileReadyCb cb) override {
    return FileEventPtr{createFileEvent_(fd, cb)};
  }

  Filesystem::WatcherPtr createFilesystemWatcher() override {
    return Filesystem::WatcherPtr{createFilesystemWatcher_()};
  }

  Network::ListenerPtr createListener(Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                      Stats::Store& stats_store, bool bind_to_port,
                                      bool use_proxy_proto, bool use_original_dst) override {
    return Network::ListenerPtr{
        createListener_(socket, cb, stats_store, bind_to_port, use_proxy_proto, use_original_dst)};
  }

  Network::ListenerPtr createSslListener(Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                         Network::ListenerCallbacks& cb, Stats::Store& stats_store,
                                         bool bind_to_port, bool use_proxy_proto,
                                         bool use_original_dst) override {
    return Network::ListenerPtr{createSslListener_(ssl_ctx, socket, cb, stats_store, bind_to_port,
                                                   use_proxy_proto, use_original_dst)};
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
  MOCK_METHOD2(createFileEvent_, FileEvent*(int fd, FileReadyCb cb));
  MOCK_METHOD0(createFilesystemWatcher_, Filesystem::Watcher*());
  MOCK_METHOD6(createListener_,
               Network::Listener*(Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                  Stats::Store& stats_store, bool bind_to_port,
                                  bool use_proxy_proto, bool use_original_dst));
  MOCK_METHOD7(createSslListener_,
               Network::Listener*(Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                  Network::ListenerCallbacks& cb, Stats::Store& stats_store,
                                  bool bind_to_port, bool use_proxy_proto, bool use_original_dst));
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

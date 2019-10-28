#pragma once

#include <unordered_map>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Upstream {

class UpstreamConnectionPoolImpl : public UpstreamConnectionPool {
public:
  UpstreamConnectionPoolImpl(Event::Dispatcher& dispatcher);
  ~UpstreamConnectionPoolImpl() override;
  void OfferConnection(HostConstSharedPtr host,
                       UpstreamConnectionEssence connection_essence) override;
  bool RetrieveConnection(
      HostConstSharedPtr host, Event::Dispatcher& dispatcher,
      std::function<void(UpstreamConnectionEssence connection_essence)> accept_cb) override;

private:
  struct ActiveClient : public LinkedObject<ActiveClient>,
                        public Network::ConnectionCallbacks,
                        public Event::DeferredDeletable {
    ActiveClient(UpstreamConnectionPoolImpl& parent, HostConstSharedPtr host,
                 Network::ClientConnectionPtr client_connection, uint64_t remaining_requests);
    UpstreamConnectionEssence detachSockets();

    void closeClientCnnection();
    void onTimeout();

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(*this, event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    UpstreamConnectionPoolImpl& parent_;
    HostConstSharedPtr host_;
    Network::ClientConnectionPtr client_connection_;
    uint64_t remaining_requests_;
    Event::TimerPtr timeout_timer_;
  };

  using ActiveClientPtr = std::unique_ptr<ActiveClient>;

  struct ConnectionPool {
    std::list<ActiveClientPtr> clients;
    // Note, available connections can become negative.
    int64_t available_connections = 0;
  };

  class CloseConnectionReadFilter : public Network::ReadFilter {
  public:
    CloseConnectionReadFilter(ActiveClient& active_client) : active_client_(active_client) {}

    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
    Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
    void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& /*callbacks*/) override{};

  private:
    ActiveClient& active_client_;
  };

  void AcceptConnectionInDispatcherThread(HostConstSharedPtr host,
                                          UpstreamConnectionEssence connection_essence);
  void RetrieveConnectionInDispatcherThread(
      HostConstSharedPtr host, Event::Dispatcher& dispatcher,
      std::function<void(UpstreamConnectionEssence connection_essence)> accept_cb);
  void onEvent(ActiveClient& client, Network::ConnectionEvent event);

  Event::Dispatcher& dispatcher_;

  Thread::MutexBasicLockable mu_;
  // Pooled connections by Host.
  // TODO cleanup this map somehow... possibly a periodic scan?  On host
  // removal?  On cluster removal?  Tracking by cluster may help, need
  // additional data.
  std::unordered_map<HostConstSharedPtr, ConnectionPool> connection_pools_ ABSL_GUARDED_BY(mu_);
};

class UpstreamConnectionPoolThread {
public:
  UpstreamConnectionPoolThread(Api::Api& api);
  ~UpstreamConnectionPoolThread();
  void start();
  void stop();

  UpstreamConnectionPool& upstream_connection_pool() { return *upstream_connection_pool_; }

private:
  Api::Api& api_;
  Event::DispatcherPtr dispatcher_;
  Thread::ThreadPtr thread_;
  std::unique_ptr<UpstreamConnectionPoolImpl> upstream_connection_pool_;
};

} // namespace Upstream
} // namespace Envoy

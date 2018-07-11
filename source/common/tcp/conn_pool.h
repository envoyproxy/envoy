#pragma once

#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/network/filter_impl.h"

namespace Envoy {
namespace Tcp {

class ConnPoolImpl : Logger::Loggable<Logger::Id::pool>, public ConnectionPool::Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options);

  ~ConnPoolImpl();

  // ConnectionPool::Instance
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newConnection(ConnectionPool::Callbacks& callbacks) override;

protected:
  struct ActiveConn;

  struct ConnectionWrapper : public ConnectionPool::ConnectionData {
    ConnectionWrapper(ActiveConn& parent);
    ~ConnectionWrapper();

    // ConnectionPool::ConnectionData
    Network::ClientConnection& connection() override;
    void addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& callbacks) override;
    void release() override;

    ActiveConn& parent_;
    ConnectionPool::UpstreamCallbacks* callbacks_{};
    bool released_{false};
  };

  typedef std::unique_ptr<ConnectionWrapper> ConnectionWrapperPtr;

  struct ConnReadFilter : public Network::ReadFilterBaseImpl {
    ConnReadFilter(ActiveConn& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) {
      parent_.onUpstreamData(data, end_stream);
      return Network::FilterStatus::StopIteration;
    }

    ActiveConn& parent_;
  };

  struct ActiveConn : LinkedObject<ActiveConn>,
                      public Network::ConnectionCallbacks,
                      public Event::DeferredDeletable {
    ActiveConn(ConnPoolImpl& parent);
    ~ActiveConn();

    void onConnectTimeout();
    void onUpstreamData(Buffer::Instance& data, bool end_stream);

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      parent_.onConnectionEvent(*this, event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ConnPoolImpl& parent_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    ConnectionWrapperPtr wrapper_;
    Network::ClientConnectionPtr conn_;
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    uint64_t remaining_requests_;
  };

  typedef std::unique_ptr<ActiveConn> ActiveConnPtr;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImpl& parent, ConnectionPool::Callbacks& callbacks);
    ~PendingRequest();

    // ConnectionPool::Cancellable
    void cancel() override { parent_.onPendingRequestCancel(*this); }

    ConnPoolImpl& parent_;
    ConnectionPool::Callbacks& callbacks_;
  };

  typedef std::unique_ptr<PendingRequest> PendingRequestPtr;

  void assignConnection(ActiveConn& conn, ConnectionPool::Callbacks& callbacks);
  void createNewConnection();
  void onConnectionEvent(ActiveConn& conn, Network::ConnectionEvent event);
  void onPendingRequestCancel(PendingRequest& request);
  virtual void onConnReleased(ActiveConn& conn);
  virtual void onConnDestroyed(ActiveConn& conn);
  void onUpstreamReady();
  void processIdleConnection(ActiveConn& conn, bool delay);
  void checkForDrained();

  Event::Dispatcher& dispatcher_;
  Upstream::HostConstSharedPtr host_;
  Upstream::ResourcePriority priority_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;

  std::list<ActiveConnPtr> ready_conns_;
  std::list<ActiveConnPtr> busy_conns_;
  std::list<PendingRequestPtr> pending_requests_;
  std::list<DrainedCb> drained_callbacks_;
  Stats::TimespanPtr conn_connect_ms_;
  Event::TimerPtr upstream_ready_timer_;
  bool upstream_ready_enabled_{false};
};

} // namespace Tcp
} // namespace Envoy

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

  struct ConnectionWrapper {
    ConnectionWrapper(ActiveConn& parent);

    Network::ClientConnection& connection();
    void addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& callbacks);
    void setConnectionState(ConnectionPool::ConnectionStatePtr&& state) {
      parent_.setConnectionState(std::move(state));
    };
    ConnectionPool::ConnectionState* connectionState() { return parent_.connectionState(); }

    void release(bool closed);

    void invalidate() { conn_valid_ = false; }

    ActiveConn& parent_;
    ConnectionPool::UpstreamCallbacks* callbacks_{};
    bool released_{false};
    bool conn_valid_{true};
  };

  typedef std::shared_ptr<ConnectionWrapper> ConnectionWrapperSharedPtr;

  struct ConnectionDataImpl : public ConnectionPool::ConnectionData {
    ConnectionDataImpl(ConnectionWrapperSharedPtr wrapper) : wrapper_(wrapper) {}
    ~ConnectionDataImpl() { wrapper_->release(false); }

    // ConnectionPool::ConnectionData
    Network::ClientConnection& connection() override { return wrapper_->connection(); }
    void addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& callbacks) override {
      wrapper_->addUpstreamCallbacks(callbacks);
    };
    void setConnectionState(ConnectionPool::ConnectionStatePtr&& state) override {
      wrapper_->setConnectionState(std::move(state));
    }
    ConnectionPool::ConnectionState* connectionState() override {
      return wrapper_->connectionState();
    }

    ConnectionWrapperSharedPtr wrapper_;
  };

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
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    void setConnectionState(ConnectionPool::ConnectionStatePtr&& state) {
      conn_state_ = std::move(state);
    }
    ConnectionPool::ConnectionState* connectionState() { return conn_state_.get(); }

    ConnPoolImpl& parent_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    ConnectionWrapperSharedPtr wrapper_;
    Network::ClientConnectionPtr conn_;
    ConnectionPool::ConnectionStatePtr conn_state_;
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    uint64_t remaining_requests_;
    bool timed_out_;
  };

  typedef std::unique_ptr<ActiveConn> ActiveConnPtr;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImpl& parent, ConnectionPool::Callbacks& callbacks);
    ~PendingRequest();

    // ConnectionPool::Cancellable
    void cancel(ConnectionPool::CancelPolicy cancel_policy) override {
      parent_.onPendingRequestCancel(*this, cancel_policy);
    }

    ConnPoolImpl& parent_;
    ConnectionPool::Callbacks& callbacks_;
  };

  typedef std::unique_ptr<PendingRequest> PendingRequestPtr;

  void assignConnection(ActiveConn& conn, ConnectionPool::Callbacks& callbacks);
  void createNewConnection();
  void onConnectionEvent(ActiveConn& conn, Network::ConnectionEvent event);
  void onPendingRequestCancel(PendingRequest& request, ConnectionPool::CancelPolicy cancel_policy);
  virtual void onConnReleased(ActiveConn& conn);
  virtual void onConnDestroyed(ActiveConn& conn);
  void onUpstreamReady();
  void processIdleConnection(ActiveConn& conn, bool new_connection, bool delay);
  void checkForDrained();

  Event::Dispatcher& dispatcher_;
  Upstream::HostConstSharedPtr host_;
  Upstream::ResourcePriority priority_;
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;

  std::list<ActiveConnPtr> pending_conns_; // conns awaiting connected event
  std::list<ActiveConnPtr> ready_conns_;   // conns ready for assignment
  std::list<ActiveConnPtr> busy_conns_;    // conns assigned
  std::list<PendingRequestPtr> pending_requests_;
  std::list<DrainedCb> drained_callbacks_;
  Stats::TimespanPtr conn_connect_ms_;
  Event::TimerPtr upstream_ready_timer_;
  bool upstream_ready_enabled_{false};
};

} // namespace Tcp
} // namespace Envoy

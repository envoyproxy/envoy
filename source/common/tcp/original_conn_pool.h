#pragma once

#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/schedulable_cb.h"
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

class OriginalConnPoolImpl : Logger::Loggable<Logger::Id::pool>, public ConnectionPool::Instance {
public:
  OriginalConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                       Upstream::ResourcePriority priority,
                       const Network::ConnectionSocket::OptionsSharedPtr& options,
                       Network::TransportSocketOptionsSharedPtr transport_socket_options);

  ~OriginalConnPoolImpl() override;

  // ConnectionPool::Instance
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  void closeConnections() override;
  ConnectionPool::Cancellable* newConnection(ConnectionPool::Callbacks& callbacks) override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; }

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

  using ConnectionWrapperSharedPtr = std::shared_ptr<ConnectionWrapper>;

  struct ConnectionDataImpl : public ConnectionPool::ConnectionData {
    ConnectionDataImpl(ConnectionWrapperSharedPtr wrapper) : wrapper_(std::move(wrapper)) {}
    ~ConnectionDataImpl() override { wrapper_->release(false); }

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
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      parent_.onUpstreamData(data, end_stream);
      return Network::FilterStatus::StopIteration;
    }

    ActiveConn& parent_;
  };

  struct ActiveConn : LinkedObject<ActiveConn>,
                      public Network::ConnectionCallbacks,
                      public Event::DeferredDeletable {
    ActiveConn(OriginalConnPoolImpl& parent);
    ~ActiveConn() override;

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

    OriginalConnPoolImpl& parent_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    ConnectionWrapperSharedPtr wrapper_;
    Network::ClientConnectionPtr conn_;
    ConnectionPool::ConnectionStatePtr conn_state_;
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    uint64_t remaining_requests_;
    bool timed_out_;
  };

  using ActiveConnPtr = std::unique_ptr<ActiveConn>;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(OriginalConnPoolImpl& parent, ConnectionPool::Callbacks& callbacks);
    ~PendingRequest() override;

    // ConnectionPool::Cancellable
    void cancel(ConnectionPool::CancelPolicy cancel_policy) override {
      parent_.onPendingRequestCancel(*this, cancel_policy);
    }

    OriginalConnPoolImpl& parent_;
    ConnectionPool::Callbacks& callbacks_;
  };

  using PendingRequestPtr = std::unique_ptr<PendingRequest>;

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
  Network::TransportSocketOptionsSharedPtr transport_socket_options_;

  std::list<ActiveConnPtr> pending_conns_; // conns awaiting connected event
  std::list<ActiveConnPtr> ready_conns_;   // conns ready for assignment
  std::list<ActiveConnPtr> busy_conns_;    // conns assigned
  std::list<PendingRequestPtr> pending_requests_;
  std::list<DrainedCb> drained_callbacks_;
  Stats::TimespanPtr conn_connect_ms_;
  Event::SchedulableCallbackPtr upstream_ready_cb_;
  bool upstream_ready_enabled_{false};
};

} // namespace Tcp
} // namespace Envoy

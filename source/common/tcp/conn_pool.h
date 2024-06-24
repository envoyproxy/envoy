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

#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/http/conn_pool_base.h"
#include "source/common/network/filter_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Tcp {

class ConnPoolImpl;

struct TcpAttachContext : public Envoy::ConnectionPool::AttachContext {
  TcpAttachContext(Tcp::ConnectionPool::Callbacks* callbacks) : callbacks_(callbacks) {}
  Tcp::ConnectionPool::Callbacks* callbacks_;
};

class TcpPendingStream : public Envoy::ConnectionPool::PendingStream {
public:
  TcpPendingStream(Envoy::ConnectionPool::ConnPoolImplBase& parent, bool can_send_early_data,
                   TcpAttachContext& context)
      : Envoy::ConnectionPool::PendingStream(parent, can_send_early_data), context_(context) {}
  Envoy::ConnectionPool::AttachContext& context() override { return context_; }

  TcpAttachContext context_;
};

class ActiveTcpClient : public Envoy::ConnectionPool::ActiveClient {
public:
  struct ConnReadFilter : public Network::ReadFilterBaseImpl {
    ConnReadFilter(ActiveTcpClient& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      parent_.onUpstreamData(data, end_stream);
      return Network::FilterStatus::StopIteration;
    }
    ActiveTcpClient& parent_;
  };

  // This acts as the bridge between the ActiveTcpClient and an individual TCP connection.
  class TcpConnectionData : public Envoy::Tcp::ConnectionPool::ConnectionData {
  public:
    TcpConnectionData(ActiveTcpClient& parent, Network::ClientConnection& connection)
        : parent_(&parent), connection_(connection) {
      parent_->tcp_connection_data_ = this;
    }
    ~TcpConnectionData() override {
      // Generally it is the case that TcpConnectionData will be destroyed before the
      // ActiveTcpClient. Because ordering on the deferred delete list is not guaranteed in the
      // case of a disconnect, make sure parent_ is valid before doing clean-up.
      if (parent_) {
        parent_->clearCallbacks();
      }
    }

    Network::ClientConnection& connection() override { return connection_; }
    void setConnectionState(ConnectionPool::ConnectionStatePtr&& state) override {
      parent_->connection_state_ = std::move(state);
    }

    void addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& callbacks) override {
      parent_->callbacks_ = &callbacks;
    }
    void release() { parent_ = nullptr; }

  protected:
    ConnectionPool::ConnectionState* connectionState() override {
      return parent_->connection_state_.get();
    }

  private:
    ActiveTcpClient* parent_;
    Network::ClientConnection& connection_;
  };

  ActiveTcpClient(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                  const Upstream::HostConstSharedPtr& host, uint64_t concurrent_stream_limit,
                  absl::optional<std::chrono::milliseconds> idle_timeout);
  ~ActiveTcpClient() override;

  // Override the default's of Envoy::ConnectionPool::ActiveClient for class-specific functions.
  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override { callbacks_->onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { callbacks_->onBelowWriteBufferLowWatermark(); }

  // Undos the readDisable done in onEvent(Connected)
  void readEnableIfNew();

  void initializeReadFilters() override { connection_->initializeReadFilters(); }
  absl::optional<Http::Protocol> protocol() const override { return {}; }
  void close() override;
  uint32_t numActiveStreams() const override { return callbacks_ ? 1 : 0; }
  bool closingWithIncompleteStream() const override { return false; }
  uint64_t id() const override { return connection_->id(); }

  void onUpstreamData(Buffer::Instance& data, bool end_stream) {
    if (callbacks_) {
      callbacks_->onUpstreamData(data, end_stream);
    } else {
      close();
    }
  }
  virtual void clearCallbacks();

  // Called if the underlying connection is idle over the cluster's tcpPoolIdleTimeout()
  void onIdleTimeout();
  void disableIdleTimer();
  void setIdleTimer();

  std::shared_ptr<ConnReadFilter> read_filter_handle_;
  Envoy::ConnectionPool::ConnPoolImplBase& parent_;
  ConnectionPool::UpstreamCallbacks* callbacks_{};
  Network::ClientConnectionPtr connection_;
  ConnectionPool::ConnectionStatePtr connection_state_;
  TcpConnectionData* tcp_connection_data_{};
  bool associated_before_{};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  Event::TimerPtr idle_timer_;
};

class ConnPoolImpl : public Envoy::ConnectionPool::ConnPoolImplBase,
                     public Tcp::ConnectionPool::Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
               Upstream::ClusterConnectivityState& state,
               absl::optional<std::chrono::milliseconds> idle_timeout)
      : Envoy::ConnectionPool::ConnPoolImplBase(host, priority, dispatcher, options,
                                                transport_socket_options, state),
        idle_timeout_(idle_timeout) {}
  ~ConnPoolImpl() override { destructAllConnections(); }

  // Event::DeferredDeletable
  void deleteIsPending() override { deleteIsPendingImpl(); }

  void addIdleCallback(IdleCb cb) override { addIdleCallbackImpl(cb); }
  bool isIdle() const override { return isIdleImpl(); }
  void drainConnections(Envoy::ConnectionPool::DrainBehavior drain_behavior) override;
  void closeConnections() override;
  ConnectionPool::Cancellable* newConnection(Tcp::ConnectionPool::Callbacks& callbacks) override;
  bool maybePreconnect(float preconnect_ratio) override {
    return maybePreconnectImpl(preconnect_ratio);
  }
  ConnectionPool::Cancellable* newPendingStream(Envoy::ConnectionPool::AttachContext& context,
                                                bool can_send_early_data) override;
  Upstream::HostDescriptionConstSharedPtr host() const override {
    return Envoy::ConnectionPool::ConnPoolImplBase::host();
  }
  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;
  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                   Envoy::ConnectionPool::AttachContext& context) override;
  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view failure_reason, ConnectionPool::PoolFailureReason reason,
                     Envoy::ConnectionPool::AttachContext& context) override;
  bool enforceMaxRequests() const override { return false; }
  // These two functions exist for testing parity between old and new Tcp Connection Pools.
  virtual void onConnReleased(Envoy::ConnectionPool::ActiveClient&) {}
  virtual void onConnDestroyed() {}

  absl::optional<std::chrono::milliseconds> idle_timeout_;
};

} // namespace Tcp
} // namespace Envoy

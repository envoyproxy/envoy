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
#include "common/http/conn_pool_base.h"
#include "common/network/filter_impl.h"

namespace Envoy {
namespace Tcp {

class ConnPoolImpl;

struct TcpAttachContext : public Envoy::ConnectionPool::AttachContext {
  TcpAttachContext(Tcp::ConnectionPool::Callbacks* callbacks) : callbacks_(callbacks) {}
  Tcp::ConnectionPool::Callbacks* callbacks_;
};

class TcpPendingStream : public Envoy::ConnectionPool::PendingStream {
public:
  TcpPendingStream(Envoy::ConnectionPool::ConnPoolImplBase& parent, TcpAttachContext& context)
      : Envoy::ConnectionPool::PendingStream(parent), context_(context) {}
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
                  const Upstream::HostConstSharedPtr& host, uint64_t concurrent_stream_limit);
  ~ActiveTcpClient() override;

  // Override the default's of Envoy::ConnectionPool::ActiveClient for class-specific functions.
  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override { callbacks_->onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { callbacks_->onBelowWriteBufferLowWatermark(); }

  void close() override { connection_->close(Network::ConnectionCloseType::NoFlush); }
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

  Envoy::ConnectionPool::ConnPoolImplBase& parent_;
  ConnectionPool::UpstreamCallbacks* callbacks_{};
  Network::ClientConnectionPtr connection_;
  ConnectionPool::ConnectionStatePtr connection_state_;
  TcpConnectionData* tcp_connection_data_{};
};

class ConnPoolImpl : public Envoy::ConnectionPool::ConnPoolImplBase,
                     public Tcp::ConnectionPool::Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsSharedPtr transport_socket_options,
               Upstream::ClusterConnectivityState& state)
      : Envoy::ConnectionPool::ConnPoolImplBase(host, priority, dispatcher, options,
                                                transport_socket_options, state) {}
  ~ConnPoolImpl() override { destructAllConnections(); }

  void addDrainedCallback(DrainedCb cb) override { addDrainedCallbackImpl(cb); }
  void drainConnections() override {
    drainConnectionsImpl();
    // Legacy behavior for the TCP connection pool marks all connecting clients
    // as draining.
    for (auto& connecting_client : connecting_clients_) {
      if (connecting_client->remaining_streams_ > 1) {
        uint64_t old_limit = connecting_client->effectiveConcurrentStreamLimit();
        connecting_client->remaining_streams_ = 1;
        if (connecting_client->effectiveConcurrentStreamLimit() < old_limit) {
          decrConnectingStreamCapacity(old_limit -
                                       connecting_client->effectiveConcurrentStreamLimit());
        }
      }
    }
  }

  void closeConnections() override {
    for (auto* list : {&ready_clients_, &busy_clients_, &connecting_clients_}) {
      while (!list->empty()) {
        list->front()->close();
      }
    }
  }
  ConnectionPool::Cancellable* newConnection(Tcp::ConnectionPool::Callbacks& callbacks) override {
    TcpAttachContext context(&callbacks);
    return Envoy::ConnectionPool::ConnPoolImplBase::newStream(context);
  }
  bool maybePreconnect(float preconnect_ratio) override {
    return Envoy::ConnectionPool::ConnPoolImplBase::maybePreconnect(preconnect_ratio);
  }

  ConnectionPool::Cancellable*
  newPendingStream(Envoy::ConnectionPool::AttachContext& context) override {
    Envoy::ConnectionPool::PendingStreamPtr pending_stream =
        std::make_unique<TcpPendingStream>(*this, typedContext<TcpAttachContext>(context));
    return addPendingStream(std::move(pending_stream));
  }

  Upstream::HostDescriptionConstSharedPtr host() const override {
    return Envoy::ConnectionPool::ConnPoolImplBase::host();
  }

  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override {
    return std::make_unique<ActiveTcpClient>(*this, Envoy::ConnectionPool::ConnPoolImplBase::host(),
                                             1);
  }

  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                   Envoy::ConnectionPool::AttachContext& context) override {
    ActiveTcpClient* tcp_client = static_cast<ActiveTcpClient*>(&client);
    auto* callbacks = typedContext<TcpAttachContext>(context).callbacks_;
    std::unique_ptr<Envoy::Tcp::ConnectionPool::ConnectionData> connection_data =
        std::make_unique<ActiveTcpClient::TcpConnectionData>(*tcp_client, *tcp_client->connection_);
    callbacks->onPoolReady(std::move(connection_data), tcp_client->real_host_description_);
  }

  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view, ConnectionPool::PoolFailureReason reason,
                     Envoy::ConnectionPool::AttachContext& context) override {
    auto* callbacks = typedContext<TcpAttachContext>(context).callbacks_;
    callbacks->onPoolFailure(reason, host_description);
  }

  // These two functions exist for testing parity between old and new Tcp Connection Pools.
  virtual void onConnReleased(Envoy::ConnectionPool::ActiveClient&) {}
  virtual void onConnDestroyed() {}
};

} // namespace Tcp
} // namespace Envoy

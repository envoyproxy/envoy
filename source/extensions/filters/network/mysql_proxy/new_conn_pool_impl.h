#pragma once

#include <list>
#include <memory>

#include "common/buffer/buffer_impl.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"
#include "common/conn_pool/conn_pool_base.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/network/filter_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {
using PoolFailureReason = Tcp::ConnectionPool::PoolFailureReason;

enum class MySQLPoolFailureReason {
  // A resource overflowed and policy prevented a new connection from being created.
  Overflow = static_cast<int>(PoolFailureReason::Overflow),
  // A local connection failure took place while creating a new connection.
  LocalConnectionFailure = static_cast<int>(PoolFailureReason::LocalConnectionFailure),
  // A remote connection failure took place while creating a new connection.
  RemoteConnectionFailure = static_cast<int>(PoolFailureReason::RemoteConnectionFailure),
  // A timeout occurred while creating a new connection.
  Timeout = static_cast<int>(PoolFailureReason::Timeout),
  // A auth failure when connect to upstream
  AuthFailure,
  // A parse error when parse upstream data
  ParseFailure,
};
/**
 * MySQL Client Pool call back
 */
class ClientPoolCallBack {
public:
  virtual ~ClientPoolCallBack() = default;
  /**
   * Called when a pool error occurred and no connection could be acquired for making the request.
   * @param reason supplies the failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onPoolFailure(MySQLPoolFailureReason reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Called when a connection is available to process a request/response. Connections may be
   * released back to the pool for re-use by resetting the ConnectionDataPtr. If the connection is
   * no longer viable for reuse (e.g. due to some kind of protocol error), the underlying
   * ClientConnection should be closed to prevent its reuse.
   *
   * @param conn supplies the connection data to use.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   */
  virtual void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                           Upstream::HostDescriptionConstSharedPtr host) PURE;
};

class Instance : public Envoy::ConnectionPool::Instance, public Event::DeferredDeletable {
public:
  ~Instance() override = default;
  virtual Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callback) PURE;
  virtual void closeConnections() PURE;
};

struct MySQLAttachContext : public Envoy::ConnectionPool::AttachContext {
  MySQLAttachContext(ClientPoolCallBack* callbacks) : callbacks_(callbacks) {}
  //  replace with mysql callbacks
  ClientPoolCallBack* callbacks_;
};

class MySQLPendingStream : public Envoy::ConnectionPool::PendingStream {
public:
  MySQLPendingStream(Envoy::ConnectionPool::ConnPoolImplBase& parent, MySQLAttachContext& context)
      : Envoy::ConnectionPool::PendingStream(parent), context_(context) {}
  Envoy::ConnectionPool::AttachContext& context() override { return context_; }

  MySQLAttachContext context_;
};

class ActiveMySQLClient : public Envoy::ConnectionPool::ActiveClient {
public:
  struct Auther : public Network::ReadFilterBaseImpl, public DecoderCallbacks {
    Auther(ActiveMySQLClient& parent) : parent_(parent) {}
    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      if (decoder_) {
        decode_buffer_.move(data);
        decoder_->onData(decode_buffer_);
        return Network::FilterStatus::Continue;
      }
      return Network::FilterStatus::StopIteration;
    }
    // DecoderCallbacks
    void onProtocolError() override;
    void onNewMessage(MySQLSession::State) override;
    void onServerGreeting(ServerGreeting&) override;
    void onClientLogin(ClientLogin&) override;
    void onClientLoginResponse(ClientLoginResponse&) override;
    void onClientSwitchResponse(ClientSwitchResponse&) override;
    void onMoreClientLoginResponse(ClientLoginResponse&) override;
    void onCommand(Command&) override;
    void onCommandResponse(CommandResponse&) override;

    DecoderPtr decoder_;
    AuthMethod auth_method_{AuthMethod::Unknown};
    std::vector<uint8_t> seed_;
    std::string username_;
    std::string password_;
    Buffer::OwnedImpl decode_buffer_;
    ActiveMySQLClient& parent_;
  };

  // This acts as the bridge between the ActiveMySQLClient and an individual TCP connection.
  class TcpConnectionData : public Envoy::Tcp::ConnectionPool::ConnectionData {
  public:
    TcpConnectionData(ActiveMySQLClient& parent, Network::ClientConnection& connection)
        : parent_(&parent), connection_(connection) {
      parent_->tcp_connection_data_ = this;
    }
    ~TcpConnectionData() override {
      // Generally it is the case that TcpConnectionData will be destroyed before the
      // ActiveMySQLClient. Because ordering on the deferred delete list is not guaranteed in the
      // case of a disconnect, make sure parent_ is valid before doing clean-up.
      if (parent_) {
        parent_->clearCallbacks();
      }
    }

    Network::ClientConnection& connection() override { return connection_; }

    void setConnectionState(Tcp::ConnectionPool::ConnectionStatePtr&& state) override {
      parent_->connection_state_ = std::move(state);
    }

    void addUpstreamCallbacks(Tcp::ConnectionPool::UpstreamCallbacks& callbacks) override {
      parent_->callbacks_ = &callbacks;
    }
    void release() { parent_ = nullptr; }

  protected:
    Tcp::ConnectionPool::ConnectionState* connectionState() override {
      return parent_->connection_state_.get();
    }

  private:
    ActiveMySQLClient* parent_;
    Network::ClientConnection& connection_;
  };

  ActiveMySQLClient(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                    const Upstream::HostConstSharedPtr& host, uint64_t concurrent_stream_limit);
  ~ActiveMySQLClient() override;

  // Override the default's of Envoy::ConnectionPool::ActiveClient for class-specific functions.
  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override { callbacks_->onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { callbacks_->onBelowWriteBufferLowWatermark(); }

  bool isNewClient() { return is_new_client_; }

  absl::optional<Http::Protocol> protocol() const override { return {}; }
  void close() override { connection_->close(Network::ConnectionCloseType::NoFlush); }
  uint32_t numActiveStreams() const override { return callbacks_ ? 1 : 0; }
  bool closingWithIncompleteStream() const override { return false; }
  uint64_t id() const override { return connection_->id(); }

  virtual void clearCallbacks();

  void onFailure(MySQLPoolFailureReason);
  void onAuthPassed();
  void makeRequest(MySQLCodec&, uint8_t);

  std::shared_ptr<Auther> read_filter_handle_;
  Envoy::ConnectionPool::ConnPoolImplBase& parent_;
  Tcp::ConnectionPool::UpstreamCallbacks* callbacks_{};
  Network::ClientConnectionPtr connection_;
  Tcp::ConnectionPool::ConnectionStatePtr connection_state_;
  TcpConnectionData* tcp_connection_data_{};

  std::string username_;
  std::string password_;
  std::string db_;
  bool is_new_client_{true};
};

class ConnPoolImpl : public Envoy::ConnectionPool::ConnPoolImplBase, public Instance {
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

  ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callbacks) override {
    MySQLAttachContext context(&callbacks);
    return Envoy::ConnectionPool::ConnPoolImplBase::newStream(context);
  }

  bool maybePreconnect(float preconnect_ratio) override {
    return Envoy::ConnectionPool::ConnPoolImplBase::maybePreconnect(preconnect_ratio);
  }

  ConnectionPool::Cancellable*
  newPendingStream(Envoy::ConnectionPool::AttachContext& context) override {
    Envoy::ConnectionPool::PendingStreamPtr pending_stream =
        std::make_unique<MySQLPendingStream>(*this, typedContext<MySQLAttachContext>(context));
    return addPendingStream(std::move(pending_stream));
  }

  Upstream::HostDescriptionConstSharedPtr host() const override {
    return Envoy::ConnectionPool::ConnPoolImplBase::host();
  }

  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override {
    return std::make_unique<ActiveMySQLClient>(*this,
                                               Envoy::ConnectionPool::ConnPoolImplBase::host(), 1);
  }

  void onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                   Envoy::ConnectionPool::AttachContext& context) override {
    ActiveMySQLClient* tcp_client = static_cast<ActiveMySQLClient*>(&client);
    auto* callbacks = typedContext<MySQLAttachContext>(context).callbacks_;
    std::unique_ptr<Envoy::Tcp::ConnectionPool::ConnectionData> connection_data =
        std::make_unique<ActiveMySQLClient::TcpConnectionData>(*tcp_client,
                                                               *tcp_client->connection_);
    callbacks->onPoolReady(std::move(connection_data), tcp_client->real_host_description_);
  }

  void onPoolFailure(const Upstream::HostDescriptionConstSharedPtr& host_description,
                     absl::string_view, ConnectionPool::PoolFailureReason reason,
                     Envoy::ConnectionPool::AttachContext& context) override {
    auto* callbacks = typedContext<MySQLAttachContext>(context).callbacks_;
    callbacks->onPoolFailure(static_cast<MySQLPoolFailureReason>(reason), host_description);
  }

  // These two functions exist for testing parity between old and new Tcp Connection Pools.
  virtual void onConnReleased(Envoy::ConnectionPool::ActiveClient&) {}
  virtual void onConnDestroyed() {}
};

using ConnectionPoolPtr = std::unique_ptr<ConnPoolImpl>;

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
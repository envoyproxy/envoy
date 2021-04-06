#pragma once

#include <list>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/conn_pool/conn_pool_base.h"
#include "common/network/filter_impl.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {
class ConnPoolImpl;

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
    // Network::ReadFilter
    Auther(ActiveMySQLClient&);
    ~Auther() override = default;
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
    void onNewMessage(MySQLSession::State) override {}
    void onServerGreeting(ServerGreeting&) override;
    void onClientLogin(ClientLogin&) override;
    void onClientLoginResponse(ClientLoginResponse&) override;
    void onClientSwitchResponse(ClientSwitchResponse&) override;
    void onMoreClientLoginResponse(ClientLoginResponse&) override;
    void onCommand(Command&) override;
    void onCommandResponse(CommandResponse&) override;

    void onFailure(MySQLPoolFailureReason reason);
    std::string username() const;
    std::string password() const;
    std::string database() const;

    ActiveMySQLClient& parent_;
    DecoderPtr decoder_;
    AuthMethod auth_method_{AuthMethod::Unknown};
    std::vector<uint8_t> seed_;
    Buffer::OwnedImpl decode_buffer_;
  };

  struct MySQLReadFilter : public Network::ReadFilterBaseImpl {
    MySQLReadFilter(ActiveMySQLClient& parent) : parent_(parent) {}
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      if (parent_.callbacks_) {
        parent_.onUpstreamData(data, end_stream);
      } else {
        parent_.close();
      }
      return Network::FilterStatus::StopIteration;
    }
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

  ActiveMySQLClient(ConnPoolImpl& parent, const Upstream::HostConstSharedPtr& host,
                    uint64_t concurrent_stream_limit);
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
  void onUpstreamData(Buffer::Instance& data, bool end_stream) {
    callbacks_->onUpstreamData(data, end_stream);
  }
  void onAuthPassed();
  void makeRequest(MySQLCodec&, uint8_t);

  ConnPoolImpl& parent_;
  std::shared_ptr<Auther> read_filter_handle_;
  std::shared_ptr<MySQLReadFilter> mysql_read_filter_;
  Tcp::ConnectionPool::UpstreamCallbacks* callbacks_{};
  Network::ClientConnectionPtr connection_;
  Tcp::ConnectionPool::ConnectionStatePtr connection_state_;
  TcpConnectionData* tcp_connection_data_{};

  bool is_new_client_{true};
};

class ThreadLocalPool;

class ConnPoolImpl : public Envoy::ConnectionPool::ConnPoolImplBase, public Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               Network::TransportSocketOptionsSharedPtr transport_socket_options,
               Upstream::ClusterConnectivityState& state, ThreadLocalPool& parent)
      : Envoy::ConnectionPool::ConnPoolImplBase(host, priority, dispatcher, options,
                                                transport_socket_options, state),
        parent_(parent) {}
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
          decrClusterStreamCapacity(old_limit -
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

  Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callbacks) override {
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

  void onMySQLFailure(MySQLPoolFailureReason reason);

  ThreadLocalPool& parent_;
};

class ConnectionPoolManagerImpl;

/**
 * ThreadLocalPool, per connection pool per host.
 */
class ThreadLocalPool : public Upstream::ClusterUpdateCallbacks,
                        public ThreadLocal::ThreadLocalObject,
                        public Logger::Loggable<Logger::Id::pool> {
public:
  ThreadLocalPool(
      std::weak_ptr<ConnectionPoolManagerImpl> parent, Event::Dispatcher&,
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
      DecoderFactory& decoder_factory);
  Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callbacks);

  void onClusterAddOrUpdateNonVirtual(Upstream::ThreadLocalCluster& cluster);
  void onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added);
  void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override {
    onClusterAddOrUpdateNonVirtual(cluster);
  }
  void onClusterRemoval(const std::string& cluster_name) override;
  friend class ConnPoolImpl;
  friend class ActiveMySQLClient;

private:
  std::weak_ptr<ConnectionPoolManagerImpl> parent_;
  Event::Dispatcher& dispatcher_;
  Upstream::ThreadLocalCluster* cluster_;
  Envoy::Common::CallbackHandlePtr host_set_member_update_cb_handle_;
  Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_handle_;
  absl::node_hash_map<std::string, Upstream::HostConstSharedPtr> host_address_map_;
  absl::node_hash_map<Upstream::HostConstSharedPtr, InstancePtr> pools_;
  std::list<InstancePtr> pools_to_drain_;
  DecoderFactory& decoder_factory_;
  std::string username_;
  std::string password_;
  std::string cluster_name_;
  std::string database_;
};

using ConnectionPoolPtr = std::unique_ptr<ConnPoolImpl>;

/**
 * implementation of ConnectionPoolManager, maintain connection pools of hosts of cluster.
 */
class ConnectionPoolManagerImpl : public ConnectionPoolManager,
                                  public std::enable_shared_from_this<ConnectionPoolManagerImpl> {
public:
  ConnectionPoolManagerImpl(Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls,
                            Api::Api& api, DecoderFactory& decoder_factory);
  Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callbacks) override;
  void init(const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route);

private:
  friend class ThreadLocalPool;
  Upstream::ClusterConnectivityState cluster_manager_state_;
  Upstream::ClusterManager* cm_;
  ThreadLocal::SlotPtr tls_;
  Api::Api& api_;
  DecoderFactory& decoder_factory_;
};

class ConnectionPoolManagerFactoryImpl : public ConnectionPoolManagerFactory {
public:
  // now use default lb to choose host.
  ConnectionPoolManagerSharedPtr
  create(Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls, Api::Api& api,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         DecoderFactory& decoder_factory) override;
  static ConnectionPoolManagerFactoryImpl instance;
};

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
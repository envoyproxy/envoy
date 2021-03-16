#include <list>
#include <memory>

#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/common/linked_object.h"
#include "common/config/datasource.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnectionPool {

struct ConnectionPoolSettings {
  ConnectionPoolSettings(const std::string& db, const std::string& cluster,
                         uint32_t max_connections, uint32_t max_idle_connections,
                         uint32_t start_connections)
      : db(db), cluster(cluster), max_connections(max_connections),
        max_idle_connections(max_idle_connections), start_connections(start_connections) {}
  ConnectionPoolSettings(
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route route,
      const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::
          ConnectionPoolSettings& setting)
      : db(route.database()), cluster(route.cluster()),
        max_connections(setting.max_connections().value()),
        max_idle_connections(setting.max_idle_connections().value()),
        start_connections(setting.start_connections().value()) {}
  std::string db;
  std::string cluster;
  uint32_t max_connections;
  uint32_t max_idle_connections;
  uint32_t start_connections;
};

class InstanceImpl : public Instance, public std::enable_shared_from_this<InstanceImpl> {
public:
  InstanceImpl(ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cluster);

  // Instance
  Cancellable* newMySQLClient(ClientPoolCallBack&) override;
  void init(Upstream::ClusterManager* cm, DecoderFactory& decoder_factory,
            const ConnectionPoolSettings& config, const std::string& auth_username,
            const std::string& auth_password);
  struct ThreadLocalActiveClient;
  struct ThreadLocalClientPool;
  enum class ClientState : uint8_t {
    Uinit,
    Ready,
    Busy,
    Stop,
  };
  struct ClientWrapper {
    ClientWrapper(ThreadLocalActiveClient& parent,
                  Tcp::ConnectionPool::ConnectionDataPtr&& conn_data);
    Tcp::ConnectionPool::ConnectionData& connectionData() { return *conn_data_; }

    ThreadLocalActiveClient& parent_;
    DecoderPtr decoder_;
    Buffer::OwnedImpl decode_buffer_;
    Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  };

  using ClientWrapperSharedPtr = std::shared_ptr<ClientWrapper>;
  struct ClientDataImpl : public ClientData {
    ClientDataImpl(ClientWrapperSharedPtr wrapper);
    void resetClient(DecoderPtr&& decoder) override;
    void sendData(Buffer::Instance&) override;
    Decoder& decoder() override;
    void close() override;

    ClientWrapperSharedPtr client_wrapper_;
  };

  struct ThreadLocalActiveClient : public LinkedObject<ThreadLocalActiveClient>,
                                   public Tcp::ConnectionPool::UpstreamCallbacks,
                                   public DecoderCallbacks,
                                   public Tcp::ConnectionPool::Callbacks,
                                   public Event::DeferredDeletable,
                                   public Logger::Loggable<Logger::Id::pool> {
    ThreadLocalActiveClient(ThreadLocalClientPool& parent);
    ~ThreadLocalActiveClient() override;
    // DecoderCallbacks
    void onProtocolError() override;
    void onNewMessage(MySQLSession::State) override{};
    void onServerGreeting(ServerGreeting&) override;
    void onClientLogin(ClientLogin&) override;
    void onClientLoginResponse(ClientLoginResponse&) override;
    void onClientSwitchResponse(ClientSwitchResponse&) override;
    void onMoreClientLoginResponse(ClientLoginResponse&) override;
    void onCommand(Command&) override;
    void onCommandResponse(CommandResponse&) override;

    // ConnectionPool::UpstreamCallbacks
    void onUpstreamData(Buffer::Instance&, bool) override;
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Tcp::ConnectionPool::Callbacks
    void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;
    void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    void writeUpstream(MySQLCodec&, uint8_t);
    void onFailure(MySQLPoolFailureReason reason);
    void onPassConnectionPhase();

    ThreadLocalClientPool& parent_;
    ClientState state_{ClientState::Uinit};
    std::vector<uint8_t> old_auth_data_;
    ClientWrapperSharedPtr client_wrapper_;
  };

  struct PendingRequest : public LinkedObject<PendingRequest>, public Cancellable {
  public:
    PendingRequest(ThreadLocalClientPool& parent, ClientPoolCallBack&);
    void cancel() override;
    ThreadLocalClientPool& parent_;
    ClientPoolCallBack& callbacks_;
  };

  using ThreadLocalActiveClientPtr = std::unique_ptr<ThreadLocalActiveClient>;
  using PendingRequestPtr = std::unique_ptr<PendingRequest>;

  struct ThreadLocalClientPool : public ThreadLocal::ThreadLocalObject {
    ThreadLocalClientPool(std::shared_ptr<InstanceImpl> parent, Upstream::ClusterManager* cm,
                          Event::Dispatcher& dispatcher, const ConnectionPoolSettings& config,
                          DecoderFactory& decoder_factory, const std::string& auth_username,
                          const std::string& auth_password);
    ~ThreadLocalClientPool() override;
    Cancellable* newMySQLClient(ClientPoolCallBack&);
    void processIdleClient(ThreadLocalActiveClient&, bool new_client);
    void createNewClient();
    void initStartClients();
    void removeClient(ThreadLocalActiveClientPtr&& client);

    std::weak_ptr<InstanceImpl> parent_;
    Event::Dispatcher& dispatcher_;
    Tcp::ConnectionPool::Instance* conn_pool_{nullptr};
    std::list<PendingRequestPtr> pending_requests_;
    DecoderFactory& decoder_factory_;
    ConnectionPoolSettings config_;
    std::list<ThreadLocalActiveClientPtr> pending_clients_;
    std::list<ThreadLocalActiveClientPtr> active_clients_;
    std::list<ThreadLocalActiveClientPtr> busy_clients_;
    std::string auth_username_;
    std::string auth_password_;
  };
  friend class ConnPoolTest;

private:
  std::string cluster_name_;
  Upstream::ClusterManager* cm_;
  ThreadLocal::SlotPtr tls_;
};

class InstanceFactoryImpl : public InstanceFactory {
public:
  ClientPoolSharedPtr
  create(ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::
             ConnectionPoolSettings& setting,
         DecoderFactory& decoder_factory, const std::string& auth_username,
         const std::string& auth_password) override;
  static InstanceFactoryImpl instance_;
};

} // namespace ConnectionPool

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

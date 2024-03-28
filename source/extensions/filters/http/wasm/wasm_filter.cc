#include "source/extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

struct MyAsyncClientManager: public Grpc::AsyncClientManager{
   absl::StatusOr<Grpc::RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& ,
                            Stats::Scope& , bool ) override {return absl::StatusOr<Grpc::RawAsyncClientSharedPtr>();};

   absl::StatusOr<Grpc::RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClientWithHashKey(const Grpc::GrpcServiceConfigWithHashKey& ,
                                       Stats::Scope& , bool ) override {return absl::StatusOr<Grpc::RawAsyncClientSharedPtr>();}

   absl::StatusOr<Grpc::AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& ,
                        Stats::Scope& , bool ) override {return absl::StatusOr<Grpc::AsyncClientFactoryPtr>();}

};

class MyClusterManagerFactory: public Upstream::ClusterManagerFactory {
public:

  Upstream::ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& ) override {return nullptr;}

  virtual Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher& , Upstream::HostConstSharedPtr ,
                   Upstream::ResourcePriority , std::vector<Http::Protocol>& ,
                   const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
                       ,
                   const Network::ConnectionSocket::OptionsSharedPtr& ,
                   const Network::TransportSocketOptionsConstSharedPtr& ,
                   TimeSource& , Upstream::ClusterConnectivityState& ,
                   Http::PersistentQuicInfoPtr& ) override {return nullptr;}

  virtual Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& , Upstream::HostConstSharedPtr ,
                      Upstream::ResourcePriority ,
                      const Network::ConnectionSocket::OptionsSharedPtr& ,
                      Network::TransportSocketOptionsConstSharedPtr ,
                      Upstream::ClusterConnectivityState& ,
                      absl::optional<std::chrono::milliseconds> ) override {return nullptr;}

  virtual absl::StatusOr<std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& , Upstream::ClusterManager& ,
                   Upstream::Outlier::EventLoggerSharedPtr , bool ) override {return absl::StatusOr<std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>();};

  virtual Upstream::CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& ,
                              const xds::core::v3::ResourceLocator* ,
                              Upstream::ClusterManager& ) override {return nullptr;}

  virtual Secret::SecretManager& secretManager() override { throw 1; }

  virtual Singleton::Manager& singletonManager() override { throw 1; }
};

struct MyClusterManager : public Upstream::ClusterManager {
  bool addOrUpdateCluster(const envoy::config::cluster::v3::Cluster& ,const std::string& ) override {return true;}


  const Upstream::ClusterLbStatNames& clusterLbStatNames() const override { throw 1; }
  const Upstream::ClusterEndpointStatNames& clusterEndpointStatNames() const override {
    throw 1;
  }
  const Upstream::ClusterLoadReportStatNames& clusterLoadReportStatNames() const override {
    throw 1;
  }
  const Upstream::ClusterCircuitBreakersStatNames& clusterCircuitBreakersStatNames() const override {
    throw 1;
  }
  const Upstream::ClusterRequestResponseSizeStatNames& clusterRequestResponseSizeStatNames() const override {
    throw 1;
  }
  const Upstream::ClusterTimeoutBudgetStatNames& clusterTimeoutBudgetStatNames() const override {
    throw 1;
  }
  void drainConnections(const std::string& ,
                        DrainConnectionsHostPredicate ) override{};
  void drainConnections(DrainConnectionsHostPredicate ) override{};
  absl::Status checkActiveStaticCluster(const std::string& ) override{ return absl::OkStatus(); };
  //void notifyMissingCluster(absl::string_view) override{};

  std::shared_ptr<const envoy::config::cluster::v3::Cluster::CommonLbConfig> getCommonLbConfigPtr(
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& ) override {
    return nullptr;
  }
  Config::EdsResourcesCacheOptRef edsResourcesCache() override { return Config::EdsResourcesCacheOptRef();}
  void setPrimaryClustersInitializedCb(PrimaryClustersReadyCallback ) override {}
  void setInitializedCb(InitializationCompleteCallback ) override {}
  bool removeCluster(const std::string& ) override {return true;}
  void shutdown() override {}
  bool isShutdown() override {return true;}
  const absl::optional<std::string>& localClusterName() const override {return any;}

  ClusterInfoMaps clusters() const override {return clusters_;}
  const absl::optional<envoy::config::core::v3::BindConfig>& bindConfig() const override {return config_;}
  Upstream::ThreadLocalCluster* getThreadLocalCluster(absl::string_view ) override {return nullptr;}
  Grpc::AsyncClientManager& grpcAsyncClientManager() override {return async_manager;}
   Config::GrpcMuxSharedPtr adsMux() override {return nullptr;}
  Upstream::OdCdsApiHandlePtr
  allocateOdCdsApi(const envoy::config::core::v3::ConfigSource& ,
                   OptRef<xds::core::v3::ResourceLocator> ,
                   ProtobufMessage::ValidationVisitor& ) override {return nullptr;}
  absl::Status
  initializeSecondaryClusters(const envoy::config::bootstrap::v3::Bootstrap& ) override {return absl::Status();}
  const Upstream::ClusterConfigUpdateStatNames& clusterConfigUpdateStatNames() const override {throw 1;}
  Upstream::ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(Upstream::ClusterUpdateCallbacks& ) override {return nullptr;}
  const Upstream::ClusterTrafficStatNames& clusterStatNames() const override {throw 1;}
  const ClusterSet& primaryClusters() override {return clusterset_;}
  Upstream::ClusterManagerFactory& clusterManagerFactory() override { return c_manager_; }
  Config::SubscriptionFactory& subscriptionFactory() override { throw 1;}

  //Upstream::ClusterConfigUpdateStatNames cluster_config_update_stat_names_;
  //Upstream::ClusterLbStatNames cluster_lb_stat_names_;
  //Upstream::ClusterEndpointStatNames cluster_endpoint_stat_names_;
  //Upstream::ClusterLoadReportStatNames cluster_load_report_stat_names_;
  //Upstream::ClusterCircuitBreakersStatNames cluster_circuit_breakers_stat_names_;
  //Upstream::ClusterRequestResponseSizeStatNames cluster_request_response_size_stat_names_;
  //Upstream::ClusterTimeoutBudgetStatNames cluster_timeout_budget_stat_names_;
  //Upstream::ClusterTrafficStatNames cluster_traffic_stat_name_;
  absl::optional<std::string> any{""};

  ClusterInfoMaps clusters_;
  ClusterSet clusterset_;
  absl::optional<envoy::config::core::v3::BindConfig> config_;
  MyAsyncClientManager async_manager;
  MyClusterManagerFactory c_manager_;
} mycluster;

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::ServerFactoryContext& context) {
  auto& server = context;
  tls_slot_ = ThreadLocal::TypedSlot<Common::Wasm::PluginHandleSharedPtrThreadLocal>::makeUnique(
      server.threadLocal());

  const auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::INBOUND, server.localInfo(),
      nullptr);

  auto callback = [plugin, this](const Common::Wasm::WasmHandleSharedPtr& base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::make_shared<PluginHandleSharedPtrThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
  };

  if (!Common::Wasm::createWasm(plugin, context.scope().createScope(""), mycluster /*server.clusterManager()*/,
                                context.initManager(), server.mainThreadDispatcher(), server.api(),
                                server.lifecycleNotifier(), remote_data_provider_,
                                std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm HTTP filter {}", plugin->name_));
  }
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

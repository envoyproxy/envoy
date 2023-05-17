namespace Envoy {
namespace Http {

// Register a custom ConnPoolFactory, which will mock the upstream
// connections to the mock cluster management.
class FuzzGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.http.fuzz"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool(Upstream::ThreadLocalCluster&, bool is_connect,
                                                   const Router::RouteEntry&,
                                                   absl::optional<Envoy::Http::Protocol>,
                                                   Upstream::LoadBalancerContext*) const override {
    if (is_connect) {
      return nullptr;
    }
    // FuzzCluster* cluster = cluster_manager->selectClusterByName(route_entry.clusterName());
    // if (cluster == nullptr) {
    //   return nullptr;
    // }
    auto conn_pool = std::make_unique<Router::MockGenericConnPool>();
    // ON_CALL(*conn_pool.get(), newStream(_))
    //     .WillByDefault(Invoke([cluster, protocol](Router::GenericConnectionPoolCallbacks*
    //     request) {
    //       cluster->newUpstream(request, protocol);
    //     }));
    return conn_pool;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::http::generic::v3::GenericConnectionPoolProto>();
  }
};

} // namespace Http
} // namespace Envoy

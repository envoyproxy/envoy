#pragma once

#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/network/address_impl.h"
#include "common/upstream/upstream_impl.h"
#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "common/upstream/cluster_factory_impl.h"
#include "server/transport_socket_config_impl.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/event/dispatcher.h"

#include "envoy/stats/scope.h"
#include "extensions/clusters/well_known_names.h"

#include "external/envoy_api/envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"

#include "envoy/upstream/upstream.h"

#include "server/init_manager_impl.h"

#include "absl/synchronization/mutex.h"
#include "extensions/filters/network/common/redis/codec.h"
#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/client_impl.h"


namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class RedisCluster : public Upstream::BaseDynamicClusterImpl {
public:
  RedisCluster(const envoy::api::v2::Cluster &cluster,
               const envoy::config::cluster::redis::RedisClusterConfig &redisCluster,
               NetworkFilters::Common::Redis::Client::ClientFactory &client_factory,
               Upstream::ClusterManager &clusterManager, Runtime::Loader &runtime,
               Network::DnsResolverSharedPtr dns_resolver,
               Server::Configuration::TransportSocketFactoryContext &factory_context,
               Stats::ScopePtr &&stats_scope, bool added_via_api);


  struct ClusterSlotsRequest : public Extensions::NetworkFilters::Common::Redis::RespValue {
  public:
    ClusterSlotsRequest() : Extensions::NetworkFilters::Common::Redis::RespValue() {
      type(Extensions::NetworkFilters::Common::Redis::RespType::Array);
      std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
      values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
      values[0].asString() = "CLUSTER";
      values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
      values[1].asString() = "SLOTS";
      asArray().swap(values);
    }
  };

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:

  void startPreInit() override;

  void updateAllHosts(const Upstream::HostVector &hosts_added, const Upstream::HostVector &hosts_removed,
                      uint32_t priority);

  const envoy::api::v2::endpoint::LocalityLbEndpoints &localityLbEndpoint() const {
    // always use the first endpoint
    return load_assignment_.endpoints()[0];
  }

  const envoy::api::v2::endpoint::LbEndpoint &lbEndpoint() const {
    // always use the first endpoint
    return localityLbEndpoint().lb_endpoints()[0];
  }

  // A redis node in the Redis cluster.
  class RedisHost : public Upstream::HostImpl {
  public:

    RedisHost(Upstream::ClusterInfoConstSharedPtr cluster, const std::string &hostname,
              Network::Address::InstanceConstSharedPtr address, RedisCluster &parent, bool master)
      : Upstream::HostImpl(cluster, hostname, address, parent.lbEndpoint().metadata(),
                           parent.lbEndpoint().load_balancing_weight().value(),
                           parent.localityLbEndpoint().locality(),
                           parent.lbEndpoint().endpoint().health_check_config(),
                           parent.localityLbEndpoint().priority(), parent.lbEndpoint().health_status()),
        master_(master) {}

    bool isMaster() const { return master_; }

  private:
    const bool master_;
  };

  // Resolve the discovery endpoint
  struct DiscoveryResolveTarget {
  DiscoveryResolveTarget(RedisCluster &parent, const std::string &dns_address,
                         const envoy::api::v2::endpoint::LocalityLbEndpoints &locality_lb_endpoint,
                         const envoy::api::v2::endpoint::LbEndpoint &lb_endpoint);

  ~DiscoveryResolveTarget();

  void startResolve();

  RedisCluster &parent_;
  Network::ActiveDnsQuery *active_query_{};
  std::string dns_address_;
  const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
  const envoy::api::v2::endpoint::LbEndpoint lb_endpoint_;
  };

  typedef std::unique_ptr<DiscoveryResolveTarget> DiscoveryResolveTargetPtr;

  struct RedisDiscoverySession
    : public Extensions::NetworkFilters::Common::Redis::Client::Config,
      public Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks,
      public Network::ConnectionCallbacks {
  RedisDiscoverySession(RedisCluster &parent, NetworkFilters::Common::Redis::Client::ClientFactory &client_factory);

  ~RedisDiscoverySession();

  // Start discovery against a random host from existing hosts
  void startResolveWithRandomHost();

  // start discovery against a choosen host
  void startResolveWithHost(const Upstream::HostSharedPtr &host);

  // Extensions::NetworkFilters::Common::Redis::Client::Config
  bool disableOutlierEvents() const override { return true; }
  std::chrono::milliseconds opTimeout() const override {
    // Allow the main Health Check infra to control timeout.
    return parent_.cluster_refresh_timeout_;
  }
  bool enableHashtagging() const override { return false; }

  // Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks
  void onResponse(NetworkFilters::Common::Redis::RespValuePtr &&value) override;
  void onFailure() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  RedisCluster &parent_;
  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client_;
  Extensions::NetworkFilters::Common::Redis::Client::PoolRequest *current_request_{};
  // the slot to master node map
  std::array<std::string, 16384> cluster_slots_map_;

  Upstream::HostVector hosts_;
  const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
  Upstream::HostMap all_hosts_;
  Event::TimerPtr resolve_timer_;
  NetworkFilters::Common::Redis::Client::ClientFactory &client_factory_;
  };

  Upstream::ClusterManager &cluster_manager_;
  const std::chrono::milliseconds cluster_refresh_rate_;
  const std::chrono::milliseconds cluster_refresh_timeout_;
  std::list<DiscoveryResolveTargetPtr> discovery_resolve_targets_;
  Event::Dispatcher &dispatcher_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Network::DnsLookupFamily dns_lookup_family_;
  const envoy::api::v2::ClusterLoadAssignment load_assignment_;
  const LocalInfo::LocalInfo &local_info_;
  Runtime::RandomGenerator &random_;
  std::unique_ptr<RedisDiscoverySession> redis_discovery_session_;
};

class RedisClusterFactory
  : public Upstream::ConfigurableClusterFactoryBase<envoy::config::cluster::redis::RedisClusterConfig> {
public:
  RedisClusterFactory() : ConfigurableClusterFactoryBase(Extensions::Clusters::ClusterTypes::get().Redis) {}

private:
  Upstream::ClusterImplBaseSharedPtr createClusterWithConfig(
    const envoy::api::v2::Cluster &cluster,
    const envoy::config::cluster::redis::RedisClusterConfig &proto_config,
    Upstream::ClusterFactoryContext &context,
    Server::Configuration::TransportSocketFactoryContext &socket_factory_context,
    Stats::ScopePtr &&stats_scope) override;

};
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
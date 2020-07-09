#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.h"
#include "envoy/config/cluster/redis/redis_cluster.pb.validate.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/http/codec.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/singleton/manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/datasource.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"
#include "common/upstream/upstream_impl.h"

#include "source/extensions/clusters/redis/redis_cluster_lb.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/clusters/well_known_names.h"
#include "extensions/common/redis/cluster_refresh_manager_impl.h"
#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/codec.h"
#include "extensions/filters/network/common/redis/utility.h"
#include "extensions/filters/network/redis_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

/*
 * This class implements support for the topology part of `Redis Cluster
 * <https://redis.io/topics/cluster-spec>`_. Specifically, it allows Envoy to maintain an internal
 * representation of the topology of a Redis Cluster, and how often the topology should be
 * refreshed.
 *
 * The target Redis Cluster is obtained from the yaml config file as usual, and we choose a random
 * discovery address from DNS if there are no existing hosts (our startup condition). Otherwise, we
 * choose a random host from our known set of hosts. Then, against this host we make a topology
 * request.
 *
 * Topology requests are handled by RedisDiscoverySession, which handles the initialization of
 * the `CLUSTER SLOTS command <https://redis.io/commands/cluster-slots>`_, and the responses and
 * failure cases.
 *
 * Once the topology is fetched from Redis, the cluster will update the
 * RedisClusterLoadBalancerFactory, which will be used by the redis proxy filter for load balancing
 * purpose.
 */

class RedisCluster : public Upstream::BaseDynamicClusterImpl {
public:
  RedisCluster(const envoy::config::cluster::v3::Cluster& cluster,
               const envoy::config::cluster::redis::RedisClusterConfig& redis_cluster,
               NetworkFilters::Common::Redis::Client::ClientFactory& client_factory,
               Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime, Api::Api& api,
               Network::DnsResolverSharedPtr dns_resolver,
               Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
               Stats::ScopePtr&& stats_scope, bool added_via_api,
               ClusterSlotUpdateCallBackSharedPtr factory);

  struct ClusterSlotsRequest : public Extensions::NetworkFilters::Common::Redis::RespValue {
  public:
    ClusterSlotsRequest() {
      type(Extensions::NetworkFilters::Common::Redis::RespType::Array);
      std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
      values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
      values[0].asString() = "CLUSTER";
      values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
      values[1].asString() = "SLOTS";
      asArray().swap(values);
    }
    static ClusterSlotsRequest instance_;
  };

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  friend class RedisClusterTest;

  void startPreInit() override;

  void updateAllHosts(const Upstream::HostVector& hosts_added,
                      const Upstream::HostVector& hosts_removed, uint32_t priority);

  void onClusterSlotUpdate(ClusterSlotsPtr&&);

  void reloadHealthyHostsHelper(const Upstream::HostSharedPtr& host) override;

  const envoy::config::endpoint::v3::LocalityLbEndpoints& localityLbEndpoint() const {
    // Always use the first endpoint.
    return load_assignment_.endpoints()[0];
  }

  const envoy::config::endpoint::v3::LbEndpoint& lbEndpoint() const {
    // Always use the first endpoint.
    return localityLbEndpoint().lb_endpoints()[0];
  }

  // A redis node in the Redis cluster.
  class RedisHost : public Upstream::HostImpl {
  public:
    RedisHost(Upstream::ClusterInfoConstSharedPtr cluster, const std::string& hostname,
              Network::Address::InstanceConstSharedPtr address, RedisCluster& parent, bool primary)
        : Upstream::HostImpl(
              cluster, hostname, address,
              // TODO(zyfjeff): Created through metadata shared pool
              std::make_shared<envoy::config::core::v3::Metadata>(parent.lbEndpoint().metadata()),
              parent.lbEndpoint().load_balancing_weight().value(),
              parent.localityLbEndpoint().locality(),
              parent.lbEndpoint().endpoint().health_check_config(),
              parent.localityLbEndpoint().priority(), parent.lbEndpoint().health_status()),
          primary_(primary) {}

    bool isPrimary() const { return primary_; }

  private:
    const bool primary_;
  };

  // Resolves the discovery endpoint.
  struct DnsDiscoveryResolveTarget {
    DnsDiscoveryResolveTarget(RedisCluster& parent, const std::string& dns_address,
                              const uint32_t port);

    ~DnsDiscoveryResolveTarget();

    void startResolveDns();

    RedisCluster& parent_;
    Network::ActiveDnsQuery* active_query_{};
    const std::string dns_address_;
    const uint32_t port_;
    Event::TimerPtr resolve_timer_;
  };

  using DnsDiscoveryResolveTargetPtr = std::unique_ptr<DnsDiscoveryResolveTarget>;

  struct RedisDiscoverySession;

  struct RedisDiscoveryClient : public Network::ConnectionCallbacks {
    RedisDiscoveryClient(RedisDiscoverySession& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisDiscoverySession& parent_;
    std::string host_;
    Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client_;
  };

  using RedisDiscoveryClientPtr = std::unique_ptr<RedisDiscoveryClient>;

  struct RedisDiscoverySession
      : public Extensions::NetworkFilters::Common::Redis::Client::Config,
        public Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks {
    RedisDiscoverySession(RedisCluster& parent,
                          NetworkFilters::Common::Redis::Client::ClientFactory& client_factory);

    ~RedisDiscoverySession() override;

    void registerDiscoveryAddress(std::list<Network::DnsResponse>&& response, const uint32_t port);

    // Start discovery against a random host from existing hosts
    void startResolveRedis();

    // Extensions::NetworkFilters::Common::Redis::Client::Config
    bool disableOutlierEvents() const override { return true; }
    std::chrono::milliseconds opTimeout() const override {
      // Allow the main Health Check infra to control timeout.
      return parent_.cluster_refresh_timeout_;
    }
    bool enableHashtagging() const override { return false; }
    bool enableRedirection() const override { return false; }
    uint32_t maxBufferSizeBeforeFlush() const override { return 0; }
    std::chrono::milliseconds bufferFlushTimeoutInMs() const override { return buffer_timeout_; }
    uint32_t maxUpstreamUnknownConnections() const override { return 0; }
    bool enableCommandStats() const override { return false; }
    // For any readPolicy other than Primary, the RedisClientFactory will send a READONLY command
    // when establishing a new connection. Since we're only using this for making the "cluster
    // slots" commands, the READONLY command is not relevant in this context. We're setting it to
    // Primary to avoid the additional READONLY command.
    Extensions::NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
      return Extensions::NetworkFilters::Common::Redis::Client::ReadPolicy::Primary;
    }

    // Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks
    void onResponse(NetworkFilters::Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    // Note: Below callback isn't used in topology updates
    bool onRedirection(NetworkFilters::Common::Redis::RespValuePtr&&, const std::string&,
                       bool) override {
      return true;
    }
    void onUnexpectedResponse(const NetworkFilters::Common::Redis::RespValuePtr&);

    Network::Address::InstanceConstSharedPtr
    ProcessCluster(const NetworkFilters::Common::Redis::RespValue& value);

    RedisCluster& parent_;
    Event::Dispatcher& dispatcher_;
    std::string current_host_address_;
    Extensions::NetworkFilters::Common::Redis::Client::PoolRequest* current_request_{};
    std::unordered_map<std::string, RedisDiscoveryClientPtr> client_map_;

    std::list<Network::Address::InstanceConstSharedPtr> discovery_address_list_;

    Event::TimerPtr resolve_timer_;
    NetworkFilters::Common::Redis::Client::ClientFactory& client_factory_;
    const std::chrono::milliseconds buffer_timeout_;
    NetworkFilters::Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  };

  Upstream::ClusterManager& cluster_manager_;
  const std::chrono::milliseconds cluster_refresh_rate_;
  const std::chrono::milliseconds cluster_refresh_timeout_;
  const std::chrono::milliseconds redirect_refresh_interval_;
  const uint32_t redirect_refresh_threshold_;
  const uint32_t failure_refresh_threshold_;
  const uint32_t host_degraded_refresh_threshold_;
  std::list<DnsDiscoveryResolveTargetPtr> dns_discovery_resolve_targets_;
  Event::Dispatcher& dispatcher_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Network::DnsLookupFamily dns_lookup_family_;
  const envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
  Runtime::RandomGenerator& random_;
  RedisDiscoverySession redis_discovery_session_;
  const ClusterSlotUpdateCallBackSharedPtr lb_factory_;

  Upstream::HostVector hosts_;
  Upstream::HostMap all_hosts_;

  const std::string auth_username_;
  const std::string auth_password_;
  const std::string cluster_name_;
  const Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager_;
  const Common::Redis::ClusterRefreshManager::HandlePtr registration_handle_;
};

class RedisClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                                envoy::config::cluster::redis::RedisClusterConfig> {
public:
  RedisClusterFactory()
      : ConfigurableClusterFactoryBase(Extensions::Clusters::ClusterTypes::get().Redis) {}

private:
  friend class RedisClusterTest;

  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::config::cluster::redis::RedisClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

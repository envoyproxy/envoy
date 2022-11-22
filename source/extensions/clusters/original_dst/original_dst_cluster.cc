#include "source/extensions/clusters/original_dst/original_dst_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Upstream {

HostConstSharedPtr OriginalDstCluster::LoadBalancer::chooseHost(LoadBalancerContext* context) {
  if (context) {
    // Check if filter state override is present, if yes use it before headers and local address.
    Network::Address::InstanceConstSharedPtr dst_host = filterStateOverrideHost(context);

    // Check if override host header is present, if yes use it otherwise check local address.
    if (dst_host == nullptr) {
      dst_host = requestOverrideHost(context);
    }

    if (dst_host == nullptr) {
      const Network::Connection* connection = context->downstreamConnection();
      // The local address of the downstream connection is the original destination address,
      // if localAddressRestored() returns 'true'.
      if (connection && connection->connectionInfoProvider().localAddressRestored()) {
        dst_host = connection->connectionInfoProvider().localAddress();
      }
    }
    if (dst_host && port_override_.has_value()) {
      dst_host = Network::Utility::getAddressWithPort(*dst_host.get(), port_override_.value());
    }

    if (dst_host) {
      const Network::Address::Instance& dst_addr = *dst_host.get();
      // Check if a host with the destination address is already in the host set.
      auto it = host_map_->find(dst_addr.asString());
      if (it != host_map_->end()) {
        HostConstSharedPtr host = it->second->host_;
        ENVOY_LOG(trace, "Using existing host {} {}.", *host, host->address()->asString());
        it->second->used_ = true;
        return host;
      }
      // Add a new host
      const Network::Address::Ip* dst_ip = dst_addr.ip();
      if (dst_ip) {
        Network::Address::InstanceConstSharedPtr host_ip_port(
            Network::Utility::copyInternetAddressAndPort(*dst_ip));
        // Create a host we can use immediately.
        auto info = parent_->info();
        HostSharedPtr host(std::make_shared<HostImpl>(
            info, info->name() + dst_addr.asString(), std::move(host_ip_port), nullptr, 1,
            envoy::config::core::v3::Locality().default_instance(),
            envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
            envoy::config::core::v3::UNKNOWN, parent_->time_source_));
        ENVOY_LOG(debug, "Created host {} {}.", *host, host->address()->asString());

        // Tell the cluster about the new host
        // lambda cannot capture a member by value.
        std::weak_ptr<OriginalDstCluster> post_parent = parent_;
        parent_->dispatcher_.post([post_parent, host]() mutable {
          // The main cluster may have disappeared while this post was queued.
          if (std::shared_ptr<OriginalDstCluster> parent = post_parent.lock()) {
            parent->addHost(host);
          }
        });
        return host;
      } else {
        ENVOY_LOG(debug, "Failed to create host for {}.", dst_addr.asString());
      }
    }
  }
  // TODO(ramaraochavali): add a stat and move this log line to debug.
  ENVOY_LOG(warn, "original_dst_load_balancer: No downstream connection or no original_dst.");
  return nullptr;
}

Network::Address::InstanceConstSharedPtr
OriginalDstCluster::LoadBalancer::filterStateOverrideHost(LoadBalancerContext* context) {
  const auto* conn = context->downstreamConnection();
  if (!conn) {
    return nullptr;
  }
  const auto* dst_address =
      conn->streamInfo().filterState().getDataReadOnly<Network::DestinationAddress>(
          Network::DestinationAddress::key());
  if (!dst_address) {
    return nullptr;
  }
  return dst_address->address();
}

Network::Address::InstanceConstSharedPtr
OriginalDstCluster::LoadBalancer::requestOverrideHost(LoadBalancerContext* context) {
  if (!http_header_name_.has_value()) {
    return nullptr;
  }
  const Http::HeaderMap* downstream_headers = context->downstreamHeaders();
  if (!downstream_headers) {
    return nullptr;
  }
  Http::HeaderMap::GetResult override_header = downstream_headers->get(*http_header_name_);
  if (override_header.empty()) {
    return nullptr;
  }
  // This is an implicitly untrusted header, so per the API documentation only the first
  // value is used.
  const std::string request_override_host(override_header[0]->value().getStringView());
  Network::Address::InstanceConstSharedPtr request_host =
      Network::Utility::parseInternetAddressAndPortNoThrow(request_override_host, false);
  if (request_host == nullptr) {
    ENVOY_LOG(debug, "original_dst_load_balancer: invalid override header value. {}",
              request_override_host);
    parent_->info()->trafficStats().original_dst_host_invalid_.inc();
    return nullptr;
  }
  ENVOY_LOG(debug, "Using request override host {}.", request_override_host);
  return request_host;
}

OriginalDstCluster::OriginalDstCluster(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(server_context, config, runtime, factory_context, std::move(stats_scope),
                      added_via_api, factory_context.mainThreadDispatcher().timeSource()),
      dispatcher_(factory_context.mainThreadDispatcher()),
      cleanup_interval_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, cleanup_interval, 5000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })),
      host_map_(std::make_shared<HostMultiMap>()) {
  if (const auto& config_opt = info_->lbOriginalDstConfig(); config_opt.has_value()) {
    if (config_opt->use_http_header()) {
      http_header_name_ = config_opt->http_header_name().empty()
                              ? Http::Headers::get().EnvoyOriginalDstHost
                              : Http::LowerCaseString(config_opt->http_header_name());
    } else {
      if (!config_opt->http_header_name().empty()) {
        throw EnvoyException(fmt::format(
            "ORIGINAL_DST cluster: invalid config http_header_name={} and use_http_header is "
            "false. Set use_http_header to true if http_header_name is desired.",
            config_opt->http_header_name()));
      }
    }
    if (config_opt->has_upstream_port_override()) {
      port_override_ = config_opt->upstream_port_override().value();
    }
  }
  if (config.has_load_assignment()) {
    throw EnvoyException("ORIGINAL_DST clusters must have no load assignment configured");
  }
  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

void OriginalDstCluster::addHost(HostSharedPtr& host) {
  std::string address = host->address()->asString();
  HostMultiMapSharedPtr new_host_map = std::make_shared<HostMultiMap>(*getCurrentHostMap());
  auto it = new_host_map->find(address);
  if (it != new_host_map->end()) {
    // If the entry already exists, that means the worker that posted this host
    // had a stale host map. Because the host is potentially in that worker's
    // connection pools, we save the host in the host map hosts_ list and the
    // cluster priority set. Subsequently, the entire hosts_ list and the
    // primary host are removed collectively, once no longer in use.
    it->second->hosts_.push_back(host);
  } else {
    // The first worker that creates a host for the address defines the primary
    // host structure.
    new_host_map->emplace(address, std::make_shared<HostsForAddress>(host));
  }
  ENVOY_LOG(debug, "addHost() adding {} {}.", *host, address);
  setHostMap(new_host_map);

  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  HostVectorSharedPtr all_hosts(new HostVector(first_host_set.hosts()));
  all_hosts->emplace_back(host);
  priority_set_.updateHosts(0,
                            HostSetImpl::partitionHosts(all_hosts, HostsPerLocalityImpl::empty()),
                            {}, {std::move(host)}, {}, absl::nullopt);
}

void OriginalDstCluster::cleanup() {
  HostVectorSharedPtr keeping_hosts(new HostVector);
  HostVector to_be_removed;
  absl::flat_hash_set<absl::string_view> removed_addresses;
  auto host_map = getCurrentHostMap();
  if (!host_map->empty()) {
    ENVOY_LOG(trace, "Cleaning up stale original dst hosts.");
    for (const auto& [addr, hosts] : *host_map) {
      // Address is kept in the cluster if either of the two things happen:
      // 1) a host has been recently selected for the address; 2) none of the
      // hosts are currently in any of the connection pools.
      // The set of hosts for a single address are treated as a unit.
      //
      // Using the used_ bit is preserved for backwards compatibility and to
      // add a delay between load balancers choosing a host and grabbing a
      // handle on the host. This prevents the following interleaving:
      //
      // 1) worker 1: pools release host h
      // 2) worker 1: auto h = lb.chooseHost(&ctx);
      // 3) main: cleanup() // deletes h because h is not used by the pools
      // 4) worker 1: auto handle = h.acquireHandle();
      //
      // Because the duration between steps 2) and 4) is O(instructions), step
      // 3) will not delete h since it takes at least one cleanup_interval for
      // the host to set used_ bit for h to false.
      bool keep = false;
      if (hosts->used_) {
        keep = true;
        hosts->used_ = false; // Mark to be removed during the next round.
      } else if (Runtime::runtimeFeatureEnabled(
                     "envoy.reloadable_features.original_dst_rely_on_idle_timeout")) {
        // Check that all hosts (first, as well as others that may have been added concurrently)
        // are not in use by any connection pool.
        if (hosts->host_->used()) {
          keep = true;
        } else {
          for (const auto& host : hosts->hosts_) {
            if (host->used()) {
              keep = true;
              break;
            }
          }
        }
      }
      if (keep) {
        ENVOY_LOG(trace, "Keeping active address {}.", addr);
        keeping_hosts->emplace_back(hosts->host_);
        if (!hosts->hosts_.empty()) {
          keeping_hosts->insert(keeping_hosts->end(), hosts->hosts_.begin(), hosts->hosts_.end());
        }
      } else {
        ENVOY_LOG(trace, "Removing stale address {}.", addr);
        removed_addresses.insert(addr);
        to_be_removed.emplace_back(hosts->host_);
        if (!hosts->hosts_.empty()) {
          to_be_removed.insert(to_be_removed.end(), hosts->hosts_.begin(), hosts->hosts_.end());
        }
      }
    }
  }
  if (!to_be_removed.empty()) {
    HostMultiMapSharedPtr new_host_map = std::make_shared<HostMultiMap>(*host_map);
    for (const auto& addr : removed_addresses) {
      new_host_map->erase(addr);
    }
    setHostMap(new_host_map);
    priority_set_.updateHosts(
        0, HostSetImpl::partitionHosts(keeping_hosts, HostsPerLocalityImpl::empty()), {}, {},
        to_be_removed, absl::nullopt);
  }

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
OriginalDstClusterFactory::createClusterImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopeSharedPtr&& stats_scope) {
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    throw EnvoyException(
        fmt::format("cluster: LB policy {} is not valid for Cluster type {}. Only "
                    "'CLUSTER_PROVIDED' is allowed with cluster type 'ORIGINAL_DST'",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(cluster.lb_policy()),
                    envoy::config::cluster::v3::Cluster::DiscoveryType_Name(cluster.type())));
  }

  // TODO(mattklein123): The original DST load balancer type should be deprecated and instead
  //                     the cluster should directly supply the load balancer. This will remove
  //                     a special case and allow this cluster to be compiled out as an extension.
  auto new_cluster = std::make_shared<OriginalDstCluster>(
      server_context, cluster, context.runtime(), socket_factory_context, std::move(stats_scope),
      context.addedViaApi());
  auto lb = std::make_unique<OriginalDstCluster::ThreadAwareLoadBalancer>(new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

/**
 * Static registration for the original dst cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalDstClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy

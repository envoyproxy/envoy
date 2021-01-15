#include "common/upstream/original_dst_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/stats/scope.h"

#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {
namespace OriginalDst {

Upstream::Host::CreateConnectionData Cluster::Host::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  return HostImpl::createConnection(dispatcher, options,
                                    transport_socket_options != nullptr
                                        ? transport_socket_options
                                        : default_transport_socket_options_);
}

HostConstSharedPtr Cluster::LoadBalancer::chooseHost(LoadBalancerContext* context) {
  if (context) {
    // Check if override host header is present, if yes use it otherwise check local address.
    Network::Address::InstanceConstSharedPtr dst_host = nullptr;
    if (parent_->use_http_header_) {
      dst_host = requestOverrideHost(context);
    }
    if (dst_host == nullptr) {
      const Network::Connection* connection = context->downstreamConnection();
      // The local address of the downstream connection is the original destination address,
      // if localAddressRestored() returns 'true'.
      if (connection && connection->addressProvider().localAddressRestored()) {
        dst_host = connection->addressProvider().localAddress();
      }
    }

    if (dst_host) {
      const Network::Address::Instance& dst_addr = *dst_host.get();
      // Check if a host with the destination address is already in the host set.
      auto it = host_map_->find(dst_addr.asString());
      if (it != host_map_->end()) {
        HostSharedPtr host(it->second); // takes a reference
        ENVOY_LOG(debug, "Using existing host {}.", host->address()->asString());
        host->used(true); // Mark as used.
        return host;
      }
      // Add a new host
      const Network::Address::Ip* dst_ip = dst_addr.ip();
      if (dst_ip) {
        Network::Address::InstanceConstSharedPtr host_ip_port(
            Network::Utility::copyInternetAddressAndPort(*dst_ip));
        Network::TransportSocketOptionsSharedPtr transport_socket_options;

        // Create an override transport socket options that automatically provides both SNI as well
        // as SAN verification for the new host if the cluster has been configured with TLS.
        if (parent_->implements_secure_transport_) {
          absl::string_view sni;

          // First check if have requested server name in the downstream connection
          // This may be set by a TLS inspector listener socket
          const Network::Connection* downstream_connection = context->downstreamConnection();
          if (downstream_connection) {
            sni = downstream_connection->requestedServerName();
          }

          if (sni.length() > 0) {
            ENVOY_LOG(debug, "Using SNI from downstream connection: {}.", sni);
          } else {
            // If no SNI from downstream connection, check if have HTTP Host in downstream headers.
            const Http::HeaderMap* downstream_headers = context->downstreamHeaders();
            if (downstream_headers) {
              auto host_header = downstream_headers->get(Http::Headers::get().Host);
              if (!host_header.empty()) {
                sni = host_header[0]->value().getStringView();
                // port in Host is optional, so cut it off, if any
                const auto pos = sni.rfind(':');
                if (pos != std::string::npos) {
                  sni.remove_suffix(sni.length() - pos);
                }
                // Remove brackets if any
                if (sni.front() == '[' && sni.back() == ']') {
                  sni.remove_prefix(1);
                  sni.remove_suffix(1);
                }
                // Try parse the host as an IP address
                try {
                  Network::Utility::parseInternetAddress(std::string(sni));
                  sni = "";
                } catch (const EnvoyException&) {
                  // Parsing as an IP address failed, so it should be a domain name
                  ENVOY_LOG(debug, "Using SNI from Host header: {}.", sni);
                }
              }
            }
          }

          if (sni.length() > 0) {
            transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
                sni, std::vector<std::string>{std::string(sni)});
          }
        }

        // Create a host we can use immediately.
        auto info = parent_->info();
        HostSharedPtr host(std::make_shared<Host>(
            info, info->name() + dst_addr.asString(), std::move(host_ip_port), nullptr, 1,
            envoy::config::core::v3::Locality().default_instance(),
            envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
            envoy::config::core::v3::UNKNOWN, parent_->time_source_, transport_socket_options));
        ENVOY_LOG(debug, "Created host {}.", host->address()->asString());

        // Tell the cluster about the new host
        // lambda cannot capture a member by value.
        std::weak_ptr<Cluster> post_parent = parent_;
        parent_->dispatcher_.post([post_parent, host]() mutable {
          // The main cluster may have disappeared while this post was queued.
          if (std::shared_ptr<Cluster> parent = post_parent.lock()) {
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
Cluster::LoadBalancer::requestOverrideHost(LoadBalancerContext* context) {
  Network::Address::InstanceConstSharedPtr request_host;
  const Http::HeaderMap* downstream_headers = context->downstreamHeaders();
  Http::HeaderMap::GetResult override_header;
  if (downstream_headers) {
    override_header = downstream_headers->get(Http::Headers::get().EnvoyOriginalDstHost);
  }
  if (!override_header.empty()) {
    try {
      // This is an implicitly untrusted header, so per the API documentation only the first
      // value is used.
      const std::string request_override_host(override_header[0]->value().getStringView());
      request_host = Network::Utility::parseInternetAddressAndPort(request_override_host, false);
      ENVOY_LOG(debug, "Using request override host {}.", request_override_host);
    } catch (const Envoy::EnvoyException& e) {
      ENVOY_LOG(debug, "original_dst_load_balancer: invalid override header value. {}", e.what());
      parent_->info()->stats().original_dst_host_invalid_.inc();
    }
  }
  return request_host;
}

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(config, runtime, factory_context, std::move(stats_scope), added_via_api,
                      factory_context.dispatcher().timeSource()),
      dispatcher_(factory_context.dispatcher()),
      cleanup_interval_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, cleanup_interval, 5000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })),
      use_http_header_(info_->lbOriginalDstConfig()
                           ? info_->lbOriginalDstConfig().value().use_http_header()
                           : false),
      implements_secure_transport_(info_->transportSocketMatcher()
                                       .resolve(nullptr)
                                       .factory_.implementsSecureTransport()),
      host_map_(std::make_shared<HostMap>()) {
  // TODO(dio): Remove hosts check once the hosts field is removed.
  if (config.has_load_assignment() || !config.hidden_envoy_deprecated_hosts().empty()) {
    throw EnvoyException("ORIGINAL_DST clusters must have no load assignment or hosts configured");
  }
  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

void Cluster::addHost(HostSharedPtr& host) {
  HostMapSharedPtr new_host_map = std::make_shared<HostMap>(*getCurrentHostMap());
  auto pair = new_host_map->emplace(host->address()->asString(), host);
  bool added = pair.second;
  if (added) {
    ENVOY_LOG(debug, "addHost() adding {}", host->address()->asString());
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
}

void Cluster::cleanup() {
  HostVectorSharedPtr keeping_hosts(new HostVector);
  HostVector to_be_removed;
  ENVOY_LOG(trace, "Stale original dst hosts cleanup triggered.");
  auto host_map = getCurrentHostMap();
  if (!host_map->empty()) {
    ENVOY_LOG(trace, "Cleaning up stale original dst hosts.");
    for (const auto& pair : *host_map) {
      const std::string& addr = pair.first;
      const HostSharedPtr& host = pair.second;
      if (host->used()) {
        ENVOY_LOG(trace, "Keeping active host {}.", addr);
        keeping_hosts->emplace_back(host);
        host->used(false); // Mark to be removed during the next round.
      } else {
        ENVOY_LOG(trace, "Removing stale host {}.", addr);
        to_be_removed.emplace_back(host);
      }
    }
  }

  if (!to_be_removed.empty()) {
    HostMapSharedPtr new_host_map = std::make_shared<HostMap>(*host_map);
    for (const Upstream::HostSharedPtr& host : to_be_removed) {
      new_host_map->erase(host->address()->asString());
    }
    setHostMap(new_host_map);
    priority_set_.updateHosts(
        0, HostSetImpl::partitionHosts(keeping_hosts, HostsPerLocalityImpl::empty()), {}, {},
        to_be_removed, absl::nullopt);
  }

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> ClusterFactory::createClusterImpl(
    const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  if (cluster.lb_policy() !=
          envoy::config::cluster::v3::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB &&
      cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    throw EnvoyException(fmt::format(
        "cluster: LB policy {} is not valid for Cluster type {}. Only 'CLUSTER_PROVIDED' or "
        "'ORIGINAL_DST_LB' is allowed with cluster type 'ORIGINAL_DST'",
        envoy::config::cluster::v3::Cluster::LbPolicy_Name(cluster.lb_policy()),
        envoy::config::cluster::v3::Cluster::DiscoveryType_Name(cluster.type())));
  }

  // TODO(mattklein123): The original DST load balancer type should be deprecated and instead
  //                     the cluster should directly supply the load balancer. This will remove
  //                     a special case and allow this cluster to be compiled out as an extension.
  auto new_cluster = std::make_shared<Cluster>(cluster, context.runtime(), socket_factory_context,
                                               std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<Cluster::ThreadAwareLoadBalancer>(new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

/**
 * Static registration for the original dst cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace OriginalDst
} // namespace Upstream
} // namespace Envoy

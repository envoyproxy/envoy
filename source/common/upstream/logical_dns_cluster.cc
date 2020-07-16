#include "common/upstream/logical_dns_cluster.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

namespace {
envoy::config::endpoint::v3::ClusterLoadAssignment
convertPriority(const envoy::config::endpoint::v3::ClusterLoadAssignment& load_assignment) {
  envoy::config::endpoint::v3::ClusterLoadAssignment converted;
  converted.MergeFrom(load_assignment);

  // We convert the priority set by the configuration back to zero. This helps
  // ensure that we don't blow up later on when using zone aware routing due
  // to a check that all priorities are zero.
  //
  // Since LOGICAL_DNS is limited to exactly one host declared per load_assignment
  // (checked in the ctor in this file), we can safely just rewrite the priority
  // to zero.
  for (auto& endpoint : *converted.mutable_endpoints()) {
    endpoint.set_priority(0);
  }

  return converted;
}
} // namespace

LogicalDnsCluster::LogicalDnsCluster(
    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()),
      resolve_timer_(
          factory_context.dispatcher().createTimer([this]() -> void { startResolve(); })),
      local_info_(factory_context.localInfo()),
      load_assignment_(
          cluster.has_load_assignment()
              ? convertPriority(cluster.load_assignment())
              : Config::Utility::translateClusterHosts(cluster.hidden_envoy_deprecated_hosts())) {
  failure_backoff_strategy_ =
      Config::Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(
          cluster, dns_refresh_rate_ms_.count(), factory_context.random());

  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  if (locality_lb_endpoints.size() != 1 || locality_lb_endpoints[0].lb_endpoints().size() != 1) {
    if (cluster.has_load_assignment()) {
      throw EnvoyException(
          "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");
    } else {
      throw EnvoyException("LOGICAL_DNS clusters must have a single host");
    }
  }

  const envoy::config::core::v3::SocketAddress& socket_address =
      lbEndpoint().endpoint().address().socket_address();

  if (!socket_address.resolver_name().empty()) {
    throw EnvoyException("LOGICAL_DNS clusters must NOT have a custom resolver name set");
  }

  dns_url_ = fmt::format("tcp://{}:{}", socket_address.address(), socket_address.port_value());
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  Network::Utility::portFromTcpUrl(dns_url_);
  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);
}

void LogicalDnsCluster::startPreInit() { startResolve(); }

LogicalDnsCluster::~LogicalDnsCluster() {
  if (active_dns_query_) {
    active_dns_query_->cancel();
  }
}

void LogicalDnsCluster::startResolve() {
  std::string dns_address = Network::Utility::hostFromTcpUrl(dns_url_);
  ENVOY_LOG(debug, "starting async DNS resolution for {}", dns_address);
  info_->stats().update_attempt_.inc();

  active_dns_query_ = dns_resolver_->resolve(
      dns_address, dns_lookup_family_,
      [this, dns_address](Network::DnsResolver::ResolutionStatus status,
                          std::list<Network::DnsResponse>&& response) -> void {
        active_dns_query_ = nullptr;
        ENVOY_LOG(debug, "async DNS resolution complete for {}", dns_address);

        std::chrono::milliseconds final_refresh_rate = dns_refresh_rate_ms_;

        // If the DNS resolver successfully resolved with an empty response list, the logical DNS
        // cluster does not update. This ensures that a potentially previously resolved address does
        // not stabilize back to 0 hosts.
        if (status == Network::DnsResolver::ResolutionStatus::Success && !response.empty()) {
          info_->stats().update_success_.inc();
          // TODO(mattklein123): Move port handling into the DNS interface.
          ASSERT(response.front().address_ != nullptr);
          Network::Address::InstanceConstSharedPtr new_address =
              Network::Utility::getAddressWithPort(*(response.front().address_),
                                                   Network::Utility::portFromTcpUrl(dns_url_));

          if (!logical_host_) {
            logical_host_ = std::make_shared<LogicalHost>(
                info_, hostname_, new_address, localityLbEndpoint(), lbEndpoint(), nullptr);

            const auto& locality_lb_endpoint = localityLbEndpoint();
            PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
            priority_state_manager.initializePriorityFor(locality_lb_endpoint);
            priority_state_manager.registerHostForPriority(logical_host_, locality_lb_endpoint);

            const uint32_t priority = locality_lb_endpoint.priority();
            priority_state_manager.updateClusterPrioritySet(
                priority, std::move(priority_state_manager.priorityState()[priority].first),
                absl::nullopt, absl::nullopt, absl::nullopt);
          }

          if (!current_resolved_address_ || !(*new_address == *current_resolved_address_)) {
            current_resolved_address_ = new_address;

            // Make sure that we have an updated address for admin display, health
            // checking, and creating real host connections.
            logical_host_->setNewAddress(new_address, lbEndpoint());
          }

          // reset failure backoff strategy because there was a success.
          failure_backoff_strategy_->reset();

          if (respect_dns_ttl_ && response.front().ttl_ != std::chrono::seconds(0)) {
            final_refresh_rate = response.front().ttl_;
          }
          ENVOY_LOG(debug, "DNS refresh rate reset for {}, refresh rate {} ms", dns_address,
                    final_refresh_rate.count());
        } else {
          info_->stats().update_failure_.inc();
          final_refresh_rate =
              std::chrono::milliseconds(failure_backoff_strategy_->nextBackOffMs());
          ENVOY_LOG(debug, "DNS refresh rate reset for {}, (failure) refresh rate {} ms",
                    dns_address, final_refresh_rate.count());
        }

        onPreInitComplete();
        resolve_timer_->enableTimer(final_refresh_rate);
      });
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
LogicalDnsClusterFactory::createClusterImpl(
    const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto selected_dns_resolver = selectDnsResolver(cluster, context);

  return std::make_pair(std::make_shared<LogicalDnsCluster>(
                            cluster, context.runtime(), selected_dns_resolver,
                            socket_factory_context, std::move(stats_scope), context.addedViaApi()),
                        nullptr);
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(LogicalDnsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy

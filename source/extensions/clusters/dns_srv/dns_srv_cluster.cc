#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include <sys/_types/_intptr_t.h>

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

#include "source/common/common/dns_utils.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

namespace {
[[gnu::unused]] envoy::config::endpoint::v3::ClusterLoadAssignment
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

DnsSrvCluster::DnsSrvCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& dns_srv_cluster,
    ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
    absl::Status& creation_status)
    : BaseDynamicClusterImpl(cluster, context, creation_status), cluster_(cluster),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()),
      resolve_timer_(context.serverFactoryContext().mainThreadDispatcher().createTimer(
          [this]() -> void { startResolve(); })),
      // load_assignment_(cluster.load_assignment()),
      local_info_(context.serverFactoryContext().localInfo()),
      // locality_lb_endpoints_(cluster.load_assignment().endpoints()),
      // load_assignment_(convertPriority(cluster.load_assignment())),
      dns_srv_cluster_(dns_srv_cluster) {

  for (const auto& n : dns_srv_cluster_.srv_names()) {
    ENVOY_LOG(info, "DnsSrvCluster::DnsSrvCluster {}", n.srv_name());
    load_assignment_.add_endpoints()->add_lb_endpoints()->set_endpoint_name(n.srv_name());
  }
  ENVOY_LOG(info, "starting async DNS resolution for, length {}",
            dns_srv_cluster_.srv_names().size());
  ENVOY_LOG(info, "starting async DNS resolution for, length {}",
            dns_srv_cluster.srv_names().size());

  // dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);
}

void DnsSrvCluster::startPreInit() {
  startResolve();
  if (!wait_for_warm_on_init_) {
    onPreInitComplete();
  }
}

DnsSrvCluster::~DnsSrvCluster() {
  if (active_dns_query_) {
    active_dns_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
}

void DnsSrvCluster::startResolve() {
  ENVOY_LOG(info, "starting async DNS resolution for, length {}",
            dns_srv_cluster_.srv_names().size());
  // info_->configUpdateStats().update_attempt_.inc();

  active_dns_query_ = dns_resolver_->resolveSrv(
      // TODO get DnsLookupFamily from config
      dns_srv_cluster_.srv_names()[0].srv_name(), Network::DnsLookupFamily::V4Only,
      [this](Network::DnsResolver::ResolutionStatus status [[gnu::unused]],
             absl::string_view details [[gnu::unused]],
             std::list<Network::DnsResponse>&& response) -> void {
        ENVOY_LOG(info, "Got DNS response");
        ENVOY_LOG(info, "details: {}", details);

        // PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
        HostVector new_hosts;
        absl::flat_hash_set<std::string> all_new_hosts;

        const auto& locality_lb_endpoints = load_assignment_.endpoints()[0];
        const auto& lb_endpoint_ = load_assignment_.endpoints()[0].lb_endpoints()[0];

        PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
        priority_state_manager.initializePriorityFor(locality_lb_endpoints);

        for (const auto& dns : response) {
          ENVOY_LOG(info, "SRV: host: {}, port: {}, weight: {}, prio: {}", dns.srv().host_,
                    dns.srv().port_, dns.srv().weight_, dns.srv().priority_);
          // TODO: dns_resolver_->resolve(dns.srv().host_);
          Network::Address::Ipv4Instance ipv4_address("127.0.0.1");
          auto address = Network::Utility::getAddressWithPort(ipv4_address, dns.srv().port_);

          if (all_new_hosts.count(address->asString()) > 0) {
            continue;
          }
          // all_hosts.push_back(address);

          // const auto& load_assignment = cluster_.load_assignment();

          ENVOY_LOG(info, "Endpoints size: {}", load_assignment_.endpoints().size());
          // load_assignment_.endpoints()[0].lb_endpoints()
          new_hosts.emplace_back(new HostImpl(
              info_, dns.srv().host_, address,
              // TODO(zyfjeff): Created through metadata shared pool
              std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint_.metadata()),
              std::make_shared<const envoy::config::core::v3::Metadata>(
                  locality_lb_endpoints.metadata()),
              lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoints.locality(),
              lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoints.priority(),
              lb_endpoint_.health_status(), time_source_));
          all_new_hosts.emplace(address->asString());

          priority_state_manager.registerHostForPriority(new_hosts.back(), locality_lb_endpoints);
        }

        HostVector all_hosts;
        HostVector hosts_added;
        HostVector hosts_removed;
        HostVector hosts_;

        updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, all_hosts_,
                              all_new_hosts);

        auto weighted_priority_health_ = load_assignment_.policy().weighted_priority_health();
        auto current_priority = locality_lb_endpoints.priority();
        auto overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
            load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);

        priority_state_manager.updateClusterPrioritySet(
            current_priority,
            std::move(priority_state_manager.priorityState()[current_priority].first), hosts_added,
            hosts_removed, absl::nullopt, weighted_priority_health_, overprovisioning_factor_);

        onPreInitComplete();
      });

  //   active_dns_query_ = dns_resolver_->resolve(
  //       dns_address_, dns_lookup_family_,
  //       [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
  //              std::list<Network::DnsResponse>&& response) -> void {
  //         active_dns_query_ = nullptr;
  //         ENVOY_LOG(trace, "async DNS resolution complete for {} details {}", dns_address_,
  //         details);

  //         std::chrono::milliseconds final_refresh_rate = dns_refresh_rate_ms_;

  //         // If the DNS resolver successfully resolved with an empty response list, the logical
  //         DNS
  //         // cluster does not update. This ensures that a potentially previously resolved address
  //         does
  //         // not stabilize back to 0 hosts.
  //         if (status == Network::DnsResolver::ResolutionStatus::Success && !response.empty()) {
  //           info_->configUpdateStats().update_success_.inc();
  //           const auto addrinfo = response.front().addrInfo();
  //           // TODO(mattklein123): Move port handling into the DNS interface.
  //           ASSERT(addrinfo.address_ != nullptr);
  //           Network::Address::InstanceConstSharedPtr new_address =
  //               Network::Utility::getAddressWithPort(*(response.front().addrInfo().address_),
  //                                                    dns_port_);
  //           auto address_list = DnsUtils::generateAddressList(response, dns_port_);
  // /*
  //           if (!logical_host_) {
  //             logical_host_ = std::make_shared<LogicalHost>(info_, hostname_, new_address,
  //                                                           address_list, localityLbEndpoint(),
  //                                                           lbEndpoint(), nullptr, time_source_);

  //             const auto& locality_lb_endpoint = localityLbEndpoint();
  //             PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
  //             priority_state_manager.initializePriorityFor(locality_lb_endpoint);
  //             priority_state_manager.registerHostForPriority(logical_host_,
  //             locality_lb_endpoint);

  //             const uint32_t priority = locality_lb_endpoint.priority();
  //             priority_state_manager.updateClusterPrioritySet(
  //                 priority, std::move(priority_state_manager.priorityState()[priority].first),
  //                 absl::nullopt, absl::nullopt, absl::nullopt, absl::nullopt, absl::nullopt);
  //           }
  // */
  // /*
  //           if (!current_resolved_address_ ||
  //               (*new_address != *current_resolved_address_ ||
  //                DnsUtils::listChanged(address_list, current_resolved_address_list_))) {
  //             current_resolved_address_ = new_address;
  //             current_resolved_address_list_ = address_list;

  //             // Make sure that we have an updated address for admin display, health
  //             // checking, and creating real host connections.
  //             logical_host_->setNewAddresses(new_address, address_list, lbEndpoint());
  //           }
  // */

  //           // reset failure backoff strategy because there was a success.
  //           // failure_backoff_strategy_->reset();

  //           if (respect_dns_ttl_ && addrinfo.ttl_ != std::chrono::seconds(0)) {
  //             final_refresh_rate = addrinfo.ttl_;
  //           }
  //           ENVOY_LOG(debug, "DNS refresh rate reset for {}, refresh rate {} ms", dns_address_,
  //                     final_refresh_rate.count());
  //         } else {
  //           info_->configUpdateStats().update_failure_.inc();
  //           final_refresh_rate =
  //               std::chrono::milliseconds(failure_backoff_strategy_->nextBackOffMs());
  //           ENVOY_LOG(debug, "DNS refresh rate reset for {}, (failure) refresh rate {} ms",
  //                     dns_address_, final_refresh_rate.count());
  //         }

  //         onPreInitComplete();
  //         resolve_timer_->enableTimer(final_refresh_rate);
  //       });
}

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
DnsSrvClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  auto dns_resolver_or_error = selectDnsResolver(cluster, context);
  THROW_IF_NOT_OK(dns_resolver_or_error.status());

  // const auto& load_assignment = cluster.load_assignment();
  // const auto& locality_lb_endpoints = load_assignment.endpoints();
  // if (locality_lb_endpoints.size() != 1 || locality_lb_endpoints[0].lb_endpoints().size() != 1) {
  //   if (cluster.has_load_assignment()) {
  //     return absl::InvalidArgumentError(
  //         "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single
  //         lb_endpoint");
  //   } else {
  //     return absl::InvalidArgumentError("LOGICAL_DNS clusters must have a single host");
  //   }
  // }

  // const envoy::config::core::v3::SocketAddress& socket_address =
  //     locality_lb_endpoints[0].lb_endpoints()[0].endpoint().address().socket_address();
  // if (!socket_address.resolver_name().empty()) {
  //   return absl::InvalidArgumentError(
  //       "LOGICAL_DNS clusters must NOT have a custom resolver name set");
  // }

  absl::Status creation_status = absl::OkStatus();
  auto ret = std::make_pair(std::shared_ptr<DnsSrvCluster>(new DnsSrvCluster(
                                cluster, proto_config, context,
                                std::move(dns_resolver_or_error.value()), creation_status)),
                            nullptr);
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DnsSrvClusterFactory, Upstream::ClusterFactory);

} // namespace Upstream
} // namespace Envoy

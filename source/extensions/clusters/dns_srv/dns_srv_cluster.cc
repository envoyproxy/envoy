#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include <sys/_types/_intptr_t.h>

#include <algorithm>
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
      event_dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      // load_assignment_(cluster.load_assignment()),
      local_info_(context.serverFactoryContext().localInfo()),
      // locality_lb_endpoints_(cluster.load_assignment().endpoints()),
      // load_assignment_(convertPriority(cluster.load_assignment())),
      dns_srv_cluster_(dns_srv_cluster) {

  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

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

  active_resolve_list_.reset(new ResolveList(*this));

  active_dns_query_ = dns_resolver_->resolveSrv(
      // TODO get DnsLookupFamily from config
      dns_srv_cluster_.srv_names()[0].srv_name(), dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status [[gnu::unused]],
             absl::string_view details [[gnu::unused]],
             std::list<Network::DnsResponse>&& response) -> void {
        active_dns_query_ = nullptr;

        ENVOY_LOG(info, "Got DNS response");
        ENVOY_LOG(info, "details: {}", details);

        // PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
        for (const auto& dns : response) {
          ENVOY_LOG(info, "SRV: host: {}, port: {}, weight: {}, prio: {}", dns.srv().host_,
                    dns.srv().port_, dns.srv().weight_, dns.srv().priority_);

          active_resolve_list_->addTarget(ResolveTargetPtr(
              new ResolveTarget(*active_resolve_list_, dns_resolver_, dns_lookup_family_,
                                dns.srv().host_, dns.srv().port_)));
        }

        active_resolve_list_->noMoreTargets();
      });
}

void DnsSrvCluster::allTargetsResolved() {
  ASSERT(active_resolve_list_.get() != nullptr);

  HostVector new_hosts;
  absl::flat_hash_set<std::string> all_new_hosts;

  const auto& locality_lb_endpoints = load_assignment_.endpoints()[0];
  const auto& lb_endpoint_ = load_assignment_.endpoints()[0].lb_endpoints()[0];

  PriorityStateManager priority_state_manager(*this, local_info_, nullptr, random_);
  priority_state_manager.initializePriorityFor(locality_lb_endpoints);

  for (const auto& target : active_resolve_list_->getResolvedTargets()) {
    // SRV query returns a number of instantances (hostnames), but each hostname
    // may potentially be resolved in a number of IP addressess
    for (auto& address : target->resolved_targets_) {

      if (all_new_hosts.count(address->asString()) > 0) {
        continue;
      }

      ENVOY_LOG(info, "Endpoints size: {}", load_assignment_.endpoints().size());
      // load_assignment_.endpoints()[0].lb_endpoints()
      new_hosts.emplace_back(new HostImpl(
          info_, target->dns_address_, address,
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
  }

  HostVector all_hosts;
  HostVector hosts_added;
  HostVector hosts_removed;
  HostVector hosts_;

  updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, all_hosts_, all_new_hosts);

  // Update host map for current resolve target.
  for (const auto& host : hosts_removed) {
    all_hosts_.erase(host->address()->asString());
  }
  for (const auto& host : hosts_added) {
    all_hosts_.insert({host->address()->asString(), host});
  }

  auto weighted_priority_health_ = load_assignment_.policy().weighted_priority_health();
  auto current_priority = locality_lb_endpoints.priority();
  auto overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      load_assignment_.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);

  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt, weighted_priority_health_,
      overprovisioning_factor_);

  onPreInitComplete();

  active_resolve_list_.reset();
  // TODO: consider TTL returned by the DNS
  resolve_timer_->enableTimer(dns_refresh_rate_ms_);
}

/////////////  DnsSrvCluster::ResolveList::ResolveList  //////////////////
DnsSrvCluster::ResolveList::ResolveList(DnsSrvCluster& parent) : parent_(parent) {}

void DnsSrvCluster::ResolveList::addTarget(ResolveTargetPtr new_target) {
  new_target->startResolve();
  active_targets_.emplace_back(std::move(new_target));
}

// callback, A/AAAA record resolved for one of the SRV record
void DnsSrvCluster::ResolveList::targetResolved(ResolveTarget* target) {
  auto p = std::find_if(
      active_targets_.begin(), active_targets_.end(),
      [target](const auto& container_value) { return container_value.get() == target; });

  ASSERT(p != active_targets_.end());

  resolved_targets_.push_back(std::move(*p));
  active_targets_.erase(p);

  maybeAllResolved();
}

void DnsSrvCluster::ResolveList::noMoreTargets() {
  no_more_targets_ = true;
  maybeAllResolved();
}

void DnsSrvCluster::ResolveList::maybeAllResolved() {
  if (active_targets_.empty() && no_more_targets_) {
    ENVOY_LOG(info, "All targets resolved for cluster {}", parent_.cluster_.name());
    parent_.allTargetsResolved();
  }
}

const std::list<DnsSrvCluster::ResolveTargetPtr>&
DnsSrvCluster::ResolveList::getResolvedTargets() const {
  return resolved_targets_;
}

/////////////  DnsSrvCluster::ResolveList::ResolveTarget  //////////////////

DnsSrvCluster::ResolveTarget::ResolveTarget(DnsSrvCluster::ResolveList& parent,
                                            Network::DnsResolverSharedPtr dns_resolver,
                                            Network::DnsLookupFamily dns_lookup_family,
                                            const std::string& dns_address, const uint32_t dns_port)
    : parent_(parent), dns_resolver_(dns_resolver), dns_lookup_family_(dns_lookup_family),
      dns_address_(dns_address), dns_port_(dns_port) {}

DnsSrvCluster::ResolveTarget::~ResolveTarget() {
  if (active_dns_query_) {
    active_dns_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
}

void DnsSrvCluster::ResolveTarget::startResolve() {
  active_dns_query_ = dns_resolver_->resolve(
      dns_address_, dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        ENVOY_LOG(info, "Resolved target '{}', details: {}, status: {}", dns_address_, details,
                  static_cast<int>(status));

        active_dns_query_ = nullptr;

        if (status == Network::DnsResolver::ResolutionStatus::Success) {
          for (const auto& resp : response) {
            const auto& addrinfo = resp.addrInfo();
            ENVOY_LOG(info, "Resolved ip for '{}' = {}", dns_address_,
                      addrinfo.address_->asStringView());

            ASSERT(addrinfo.address_ != nullptr);
            auto address = Network::Utility::getAddressWithPort(*(addrinfo.address_), dns_port_);

            resolved_targets_.push_back(address);
          }
        }

        resolve_status_ = status;
        parent_.targetResolved(this);
      });
}

/////////////  DnsSrvClusterFactory  //////////////////

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

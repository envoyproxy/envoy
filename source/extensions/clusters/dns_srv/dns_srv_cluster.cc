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

DnsSrvCluster::DnsSrvCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns_srv::v3::Cluster& dns_srv_cluster,
    ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
    absl::Status& creation_status)
    : BaseDynamicClusterImpl(cluster, context, creation_status), cluster_(cluster),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()), // TODO: use me
      resolve_timer_(context.serverFactoryContext().mainThreadDispatcher().createTimer(
          [this]() -> void { startResolve(); })),
      local_info_(context.serverFactoryContext().localInfo()), dns_srv_cluster_(dns_srv_cluster) {

  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

  for (const auto& n : dns_srv_cluster_.srv_names()) {
    ENVOY_LOG(debug, "DnsSrvCluster::DnsSrvCluster {}", n.srv_name());
    load_assignment_.add_endpoints()->add_lb_endpoints()->set_endpoint_name(n.srv_name());
  }
  ENVOY_LOG(debug, "starting async DNS resolution for, length {}",
            dns_srv_cluster_.srv_names().size());
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
  ENVOY_LOG(debug, "starting async DNS resolution for, length {}",
            dns_srv_cluster_.srv_names().size());
  info_->configUpdateStats().update_attempt_.inc();

  active_resolve_list_.reset(new ResolveList(*this));

  active_dns_query_ = dns_resolver_->resolveSrv(
      dns_srv_cluster_.srv_names()[0].srv_name(), dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        active_dns_query_ = nullptr;

        ENVOY_LOG(debug, "Got DNS response, details: {}, status: {}", details,
                  static_cast<int>(status));

        if (status == Network::DnsResolver::ResolutionStatus::Success) {
          for (const auto& dns : response) {
            ENVOY_LOG(debug, "SRV: host: {}, port: {}, weight: {}, prio: {}", dns.srv().host_,
                      dns.srv().port_, dns.srv().weight_, dns.srv().priority_);

            active_resolve_list_->addTarget(ResolveTargetPtr(
                new ResolveTarget(*active_resolve_list_, dns_resolver_, dns_lookup_family_,
                                  dns.srv().host_, dns.srv().port_)));
          }

          active_resolve_list_->noMoreTargets();
        } else {
          info_->configUpdateStats().update_failure_.inc();
          resolve_timer_->enableTimer(dns_refresh_rate_ms_);
          onPreInitComplete();
        }
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

  // Only update the list of targets if at least one of them was resolved successfully
  // this has a down-side: we can loose some of targets for some while but hopefully they'll get
  // back next time
  bool some_targets_resolved = false;

  for (const auto& target : active_resolve_list_->getResolvedTargets()) {
    // SRV query returns a number of instances (hostnames), but each hostname
    // may potentially be resolved in a number of IP addresses
    if (target->resolve_status_ != Network::DnsResolver::ResolutionStatus::Success) {
      ENVOY_LOG(debug, "IP resolution for target: '{}' has failed", target->srv_record_hostname_);
      continue;
    }

    some_targets_resolved = true;

    for (auto& address : target->resolved_targets_) {

      if (all_new_hosts.count(address->asString()) > 0) {
        continue;
      }

      ENVOY_LOG(debug, "Endpoints size: {}", load_assignment_.endpoints().size());
      // load_assignment_.endpoints()[0].lb_endpoints()
      new_hosts.emplace_back(new HostImpl(
          info_, target->srv_record_hostname_, address,
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

    info_->configUpdateStats().update_success_.inc();
  }

  if (some_targets_resolved) {
    HostVector all_hosts;
    HostVector hosts_added;
    HostVector hosts_removed;
    HostVector hosts_;

    if (updateDynamicHostList(new_hosts, hosts_, hosts_added, hosts_removed, all_hosts_,
                              all_new_hosts)) {
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
          current_priority,
          std::move(priority_state_manager.priorityState()[current_priority].first), hosts_added,
          hosts_removed, absl::nullopt, weighted_priority_health_, overprovisioning_factor_);
    } else {
      info_->configUpdateStats().update_no_rebuild_.inc();
    }
  }

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
    ENVOY_LOG(debug, "All targets resolved for cluster {}", parent_.cluster_.name());
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
      srv_record_hostname_(dns_address), dns_port_(dns_port) {}

DnsSrvCluster::ResolveTarget::~ResolveTarget() {
  if (active_dns_query_) {
    active_dns_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
}

void DnsSrvCluster::ResolveTarget::startResolve() {
  active_dns_query_ = dns_resolver_->resolve(
      srv_record_hostname_, dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        ENVOY_LOG(debug, "Resolved target '{}', details: {}, status: {}", srv_record_hostname_,
                  details, static_cast<int>(status));

        active_dns_query_ = nullptr;

        if (status == Network::DnsResolver::ResolutionStatus::Success) {
          for (const auto& resp : response) {
            const auto& addrinfo = resp.addrInfo();
            ENVOY_LOG(debug, "Resolved ip for '{}' = {}", srv_record_hostname_,
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
    const envoy::extensions::clusters::dns_srv::v3::Cluster& proto_config,
    Upstream::ClusterFactoryContext& context) {
  auto dns_resolver_or_error = selectDnsResolver(cluster, context);
  THROW_IF_NOT_OK(dns_resolver_or_error.status());

  if (proto_config.srv_names_size() > 1) {
    return absl::InvalidArgumentError("SRV DNS Cluster can only contain one DNS record (so far)");
  }

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

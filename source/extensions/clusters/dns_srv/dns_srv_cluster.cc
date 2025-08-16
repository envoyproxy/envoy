#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include <algorithm>
#include <chrono>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/fmt.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

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
      local_info_(context.serverFactoryContext().localInfo()), dns_srv_cluster_(dns_srv_cluster) {

  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

  load_assignment_.add_endpoints()->add_lb_endpoints()->set_endpoint_name(
      dns_srv_cluster_.srv_name());

  ENVOY_LOG(debug, "starting async DNS resolution for {}", dns_srv_cluster_.srv_name());
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
  ENVOY_LOG(debug, "starting async DNS resolution for {}", dns_srv_cluster_.srv_name());
  info_->configUpdateStats().update_attempt_.inc();

  active_resolve_list_.reset(new ResolveList(*this));

  active_dns_query_ = dns_resolver_->resolveSrv(
      dns_srv_cluster_.srv_name(),
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        active_dns_query_ = nullptr;

        ENVOY_LOG(debug, "Got DNS response, details: {}, status: {}", details,
                  static_cast<int>(status));

        if (status == Network::DnsResolver::ResolutionStatus::Completed) {
          for (const auto& dns : response) {

            ENVOY_LOG(debug, "SRV: host: {}, port: {}, weight: {}, prio: {}", dns.srv().target_,
                      dns.srv().port_, dns.srv().weight_, dns.srv().priority_);

            if (auto address = Envoy::Network::Utility::parseInternetAddressNoThrow(
                    dns.srv().target_, 0, false);
                address != nullptr) {
              // SRV record target is an IP address, not a hostname.
              ResolveTargetPtr target = ResolveTargetPtr(
                  new ResolveTarget(*active_resolve_list_, dns_resolver_, dns_lookup_family_,
                                    dns.srv().target_, dns.srv().port_));

              active_resolve_list_->addResolvedTarget(std::move(target), address);
            } else {
              active_resolve_list_->addTarget(ResolveTargetPtr(
                  new ResolveTarget(*active_resolve_list_, dns_resolver_, dns_lookup_family_,
                                    dns.srv().target_, dns.srv().port_)));
            }
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
    if (target->resolve_status_ != Network::DnsResolver::ResolutionStatus::Completed) {
      ENVOY_LOG(debug, "IP resolution for target: '{}' has failed: {}",
                target->srv_record_hostname_, target->resolve_status_details_);
      continue;
    }

    some_targets_resolved = true;

    for (auto& address : target->resolved_targets_) {
      if (all_new_hosts.count(address->asString()) > 0) {
        continue;
      }

      ENVOY_LOG(debug, "Endpoints size: {}", load_assignment_.endpoints().size());

      auto host_or_status = HostImpl::create(
          info_, target->srv_record_hostname_, address,
          // TODO(zyfjeff): Created through metadata shared pool
          std::make_shared<const envoy::config::core::v3::Metadata>(lb_endpoint_.metadata()),
          std::make_shared<const envoy::config::core::v3::Metadata>(
              locality_lb_endpoints.metadata()),
          lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoints.locality(),
          lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoints.priority(),
          lb_endpoint_.health_status(), time_source_);

      if (!host_or_status.ok()) {
        info_->configUpdateStats().update_failure_.inc();

        ENVOY_LOG(debug, "Failed to create host record for: '{}': code={}, message='{}'",
                  target->srv_record_hostname_, host_or_status.status().raw_code(),
                  host_or_status.status().message());
        continue;
      }

      new_hosts.emplace_back(std::move(host_or_status.value()));
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

  std::chrono::milliseconds final_refresh_rate_ms = dns_refresh_rate_ms_;
  if (respect_dns_ttl_) {
    std::chrono::seconds dns_ttl = active_resolve_list_->dnsTtlRefreshRate();
    if (dns_ttl != std::chrono::seconds::max() && dns_ttl.count() > 0) {
      final_refresh_rate_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dns_ttl);
    }
  }

  active_resolve_list_.reset();

  resolve_timer_->enableTimer(final_refresh_rate_ms);
}

/////////////  DnsSrvCluster::ResolveList::ResolveList  //////////////////
DnsSrvCluster::ResolveList::ResolveList(DnsSrvCluster& parent)
    : parent_(parent), dns_ttl_refresh_rate_(std::chrono::seconds::max()) {}

void DnsSrvCluster::ResolveList::addTarget(ResolveTargetPtr new_target) {
  auto target_ptr = new_target.get();
  active_targets_.emplace_back(std::move(new_target));
  target_ptr->startResolve();
}

// In case when SRV record target is an IP address, not a hostname
void DnsSrvCluster::ResolveList::addResolvedTarget(
    ResolveTargetPtr new_target, Network::Address::InstanceConstSharedPtr resolved_address) {
  auto target_ptr = new_target.get();
  resolved_targets_.emplace_back(std::move(new_target));
  target_ptr->addResolvedTarget(resolved_address);
}

// callback, A/AAAA record resolved for one of the SRV record
void DnsSrvCluster::ResolveList::targetResolved(ResolveTarget* target,
                                                std::chrono::seconds dns_ttl) {
  auto p = std::find_if(
      active_targets_.begin(), active_targets_.end(),
      [target](const auto& container_value) { return container_value.get() == target; });

  ASSERT(p != active_targets_.end());

  resolved_targets_.push_back(std::move(*p));
  active_targets_.erase(p);

  dns_ttl_refresh_rate_ = std::min(dns_ttl_refresh_rate_, dns_ttl);

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

std::chrono::seconds DnsSrvCluster::ResolveList::dnsTtlRefreshRate() const {
  return dns_ttl_refresh_rate_;
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
        ENVOY_LOG(debug, "Resolved target '{}', details: '{}', status: {}", srv_record_hostname_,
                  details, static_cast<int>(status));

        active_dns_query_ = nullptr;

        std::chrono::seconds dns_ttl = std::chrono::seconds::max();
        if (status == Network::DnsResolver::ResolutionStatus::Completed) {
          for (const auto& resp : response) {
            const auto& addrinfo = resp.addrInfo();
            ENVOY_LOG(debug, "Resolved ip for '{}' = {}", srv_record_hostname_,
                      addrinfo.address_->asStringView());

            ASSERT(addrinfo.address_ != nullptr);
            auto address = Network::Utility::getAddressWithPort(*(addrinfo.address_), dns_port_);

            resolved_targets_.push_back(address);
            dns_ttl = std::min(dns_ttl, addrinfo.ttl_);
          }
        }

        resolve_status_ = status;
        resolve_status_details_ = details;
        parent_.targetResolved(this, dns_ttl);
      });
}

void DnsSrvCluster::ResolveTarget::addResolvedTarget(
    Network::Address::InstanceConstSharedPtr address) {
  resolved_targets_.push_back(std::move(address));
  parent_.targetResolved(this, std::chrono::seconds::max());
}

/////////////  DnsSrvClusterFactory  //////////////////

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
DnsSrvClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  auto dns_resolver_or_error = selectDnsResolver(cluster, context);
  RETURN_IF_NOT_OK(dns_resolver_or_error.status());

  if (cluster.typed_dns_resolver_config().name() != "envoy.network.dns_resolver.cares") {
    return absl::InvalidArgumentError(
        fmt::format("Only c-ares supports resolve of SRV records, "
                    "please use typed_dns_resolver_config.name = "
                    "'envoy.network.dns_resolver.cares'. Current value: '{}'",
                    cluster.typed_dns_resolver_config().name()));
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

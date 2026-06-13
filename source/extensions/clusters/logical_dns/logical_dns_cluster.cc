#include "source/extensions/clusters/logical_dns/logical_dns_cluster.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/dns_utils.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/clusters/common/dns_cluster_backcompat.h"

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

absl::StatusOr<std::unique_ptr<LogicalDnsCluster>>
LogicalDnsCluster::create(const envoy::config::cluster::v3::Cluster& cluster,
                          const envoy::extensions::clusters::dns::v3::DnsCluster& dns_cluster,
                          ClusterFactoryContext& context,
                          Network::DnsResolverSharedPtr dns_resolver) {
  const auto& load_assignment = cluster.load_assignment();
  const auto& locality_lb_endpoints = load_assignment.endpoints();
  if (locality_lb_endpoints.size() != 1 || locality_lb_endpoints[0].lb_endpoints().size() != 1) {
    if (cluster.has_load_assignment()) {
      return absl::InvalidArgumentError(
          "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");
    } else {
      return absl::InvalidArgumentError("LOGICAL_DNS clusters must have a single host");
    }
  }

  const envoy::config::core::v3::SocketAddress& socket_address =
      locality_lb_endpoints[0].lb_endpoints()[0].endpoint().address().socket_address();
  if (!socket_address.resolver_name().empty()) {
    return absl::InvalidArgumentError(
        "LOGICAL_DNS clusters must NOT have a custom resolver name set");
  }

  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<LogicalDnsCluster> ret;

  ret = std::unique_ptr<LogicalDnsCluster>(new LogicalDnsCluster(
      cluster, dns_cluster, context, std::move(dns_resolver), creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

LogicalDnsCluster::LogicalDnsCluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns::v3::DnsCluster& dns_cluster,
    ClusterFactoryContext& context, Network::DnsResolverSharedPtr dns_resolver,
    absl::Status& creation_status)
    : ClusterImplBase(cluster, context, creation_status), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(dns_cluster, dns_refresh_rate, 5000))),
      dns_jitter_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(dns_cluster, dns_jitter, 0))),
      respect_dns_ttl_(dns_cluster.respect_dns_ttl()),
      dns_lookup_family_(
          Envoy::DnsUtils::getDnsLookupFamilyFromEnum(dns_cluster.dns_lookup_family())),
      resolve_timer_(context.serverFactoryContext().mainThreadDispatcher().createTimer(
          [this]() -> void { startResolve(); })),
      local_info_(context.serverFactoryContext().localInfo()),
      load_assignment_(convertPriority(cluster.load_assignment())),
      runtime_(context.serverFactoryContext().runtime()) {
  failure_backoff_strategy_ = Config::Utility::prepareDnsRefreshStrategy(
      dns_cluster, dns_refresh_rate_ms_.count(),
      context.serverFactoryContext().api().randomGenerator());

  const envoy::config::core::v3::SocketAddress& socket_address =
      lbEndpoint().endpoint().address().socket_address();

  // Checked by factory;
  ASSERT(socket_address.resolver_name().empty());
  dns_address_ = socket_address.address();
  dns_port_ = socket_address.port_value();

  if (lbEndpoint().endpoint().hostname().empty()) {
    hostname_ = dns_address_;
  } else {
    hostname_ = lbEndpoint().endpoint().hostname();
  }
}

void LogicalDnsCluster::startPreInit() {
  startResolve();
  if (!wait_for_warm_on_init_) {
    onPreInitComplete();
  }
}

LogicalDnsCluster::~LogicalDnsCluster() {
  if (active_dns_query_) {
    active_dns_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
  if (active_ech_dns_query_) {
    active_ech_dns_query_->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  }
}

void LogicalDnsCluster::startResolve() {
  ENVOY_LOG(trace, "starting async DNS resolution for {}", dns_address_);
  info_->configUpdateStats().update_attempt_.inc();

  address_resolved_ = false;
  ech_resolved_ = false;

  active_dns_query_ = dns_resolver_->resolve(
      dns_address_, dns_lookup_family_,
      [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<Network::DnsResponse>&& response) -> void {
        active_dns_query_ = nullptr;
        ENVOY_LOG(trace, "async DNS resolution complete for {} details {}", dns_address_, details);
        address_resolved_ = true;
        address_status_ = status;
        resolved_addresses_ = std::move(response);
        onResolutionComplete();
      });

  const bool enable_ech =
      runtime_.snapshot().runtimeFeatureEnabled("envoy.reloadable_features.enable_ech");
  if (enable_ech) {
    active_ech_dns_query_ = dns_resolver_->resolveRecord(
        dns_address_, Network::RecordType::HTTPS,
        [this](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
               std::list<Network::DnsResponse>&& response) -> void {
          active_ech_dns_query_ = nullptr;
          ENVOY_LOG(trace, "async ECH DNS resolution complete for {} details {}", dns_address_,
                    details);
          ech_resolved_ = true;
          ech_status_ = status;
          if (status == Network::DnsResolver::ResolutionStatus::Completed && !response.empty()) {
            for (const auto& resp : response) {
              std::vector<uint8_t> ech_config = DnsUtils::parseHttpsRecord(resp.generic().rdata_);
              if (!ech_config.empty()) {
                resolved_ech_config_ = std::move(ech_config);
                break;
              }
            }
          }
          onResolutionComplete();
        });
  } else {
    ech_resolved_ = true;
    ech_status_ = Network::DnsResolver::ResolutionStatus::Completed;
    onResolutionComplete();
  }
}

void LogicalDnsCluster::onResolutionComplete() {
  const bool enable_ech =
      runtime_.snapshot().runtimeFeatureEnabled("envoy.reloadable_features.enable_ech");
  if (enable_ech) {
    if (!address_resolved_ || !ech_resolved_) {
      return;
    }
  } else {
    if (!address_resolved_) {
      return;
    }
  }

  std::chrono::milliseconds final_refresh_rate = dns_refresh_rate_ms_;

  if (address_status_ == Network::DnsResolver::ResolutionStatus::Completed &&
      !resolved_addresses_.empty()) {
    info_->configUpdateStats().update_success_.inc();
    const auto addrinfo = resolved_addresses_.front().addrInfo();
    Network::Address::InstanceConstSharedPtr new_address = Network::Utility::getAddressWithPort(
        *(resolved_addresses_.front().addrInfo().address_), dns_port_);
    auto address_list = DnsUtils::generateAddressList(resolved_addresses_, dns_port_);

    std::vector<uint8_t> ech_config;
    if (ech_status_ == Network::DnsResolver::ResolutionStatus::Completed &&
        !resolved_ech_config_.empty()) {
      ech_config = resolved_ech_config_;
    }

    if (!logical_host_) {
      logical_host_ = THROW_OR_RETURN_VALUE(LogicalHost::create(info_, hostname_, new_address,
                                                                address_list, localityLbEndpoint(),
                                                                lbEndpoint(), nullptr, ech_config),
                                            std::unique_ptr<LogicalHost>);

      const auto& locality_lb_endpoint = localityLbEndpoint();
      PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
      priority_state_manager.initializePriorityFor(locality_lb_endpoint);
      priority_state_manager.registerHostForPriority(logical_host_, locality_lb_endpoint);

      const uint32_t priority = locality_lb_endpoint.priority();
      priority_state_manager.updateClusterPrioritySet(
          priority, std::move(priority_state_manager.priorityState()[priority].first),
          absl::nullopt, absl::nullopt, absl::nullopt, absl::nullopt, absl::nullopt);
    }

    if (!current_resolved_address_ ||
        (*new_address != *current_resolved_address_ ||
         DnsUtils::listChanged(address_list, current_resolved_address_list_) ||
         ech_config != current_resolved_ech_config_)) {
      current_resolved_address_ = new_address;
      current_resolved_address_list_ = address_list;
      current_resolved_ech_config_ = ech_config;

      logical_host_->setNewAddresses(new_address, address_list, lbEndpoint(), ech_config);
    } else {
      info_->configUpdateStats().update_no_rebuild_.inc();
    }

    failure_backoff_strategy_->reset();

    if (respect_dns_ttl_ && addrinfo.ttl_ != std::chrono::seconds(0)) {
      final_refresh_rate = addrinfo.ttl_;
    }
    if (dns_jitter_ms_.count() != 0) {
      final_refresh_rate += std::chrono::milliseconds(random_.random() % dns_jitter_ms_.count());
    }
    ENVOY_LOG(debug, "DNS refresh rate reset for {}, refresh rate {} ms", dns_address_,
              final_refresh_rate.count());
  } else {
    info_->configUpdateStats().update_failure_.inc();
    final_refresh_rate = std::chrono::milliseconds(failure_backoff_strategy_->nextBackOffMs());
    ENVOY_LOG(debug, "DNS refresh rate reset for {}, (failure) refresh rate {} ms", dns_address_,
              final_refresh_rate.count());
  }

  address_resolved_ = false;
  if (enable_ech) {
    ech_resolved_ = false;
    resolved_ech_config_.clear();
  }
  resolved_addresses_.clear();

  onPreInitComplete();
  resolve_timer_->enableTimer(final_refresh_rate);
}

} // namespace Upstream
} // namespace Envoy

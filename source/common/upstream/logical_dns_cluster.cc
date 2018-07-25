#include "common/upstream/logical_dns_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

LogicalDnsCluster::LogicalDnsCluster(const envoy::api::v2::Cluster& cluster,
                                     Runtime::Loader& runtime, Stats::Store& stats,
                                     Ssl::ContextManager& ssl_context_manager,
                                     const LocalInfo::LocalInfo& local_info,
                                     Network::DnsResolverSharedPtr dns_resolver,
                                     ThreadLocal::SlotAllocator& tls, ClusterManager& cm,
                                     Event::Dispatcher& dispatcher, bool added_via_api)
    : ClusterImplBase(cluster, cm.bindConfig(), runtime, stats, ssl_context_manager,
                      cm.clusterManagerFactory().secretManager(), added_via_api),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      tls_(tls.allocateSlot()),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })),
      local_info_(local_info),
      load_assignment_(cluster.has_load_assignment()
                           ? cluster.load_assignment()
                           : Config::Utility::translateClusterHosts(cluster.hosts())) {
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  if (locality_lb_endpoints.size() != 1 || locality_lb_endpoints[0].lb_endpoints().size() != 1) {
    if (cluster.has_load_assignment()) {
      throw EnvoyException(
          "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");
    } else {
      throw EnvoyException("LOGICAL_DNS clusters must have a single host");
    }
  }

  switch (cluster.dns_lookup_family()) {
  case envoy::api::v2::Cluster::V6_ONLY:
    dns_lookup_family_ = Network::DnsLookupFamily::V6Only;
    break;
  case envoy::api::v2::Cluster::V4_ONLY:
    dns_lookup_family_ = Network::DnsLookupFamily::V4Only;
    break;
  case envoy::api::v2::Cluster::AUTO:
    dns_lookup_family_ = Network::DnsLookupFamily::Auto;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const envoy::api::v2::core::SocketAddress& socket_address =
      lbEndpoint().endpoint().address().socket_address();
  dns_url_ = fmt::format("tcp://{}:{}", socket_address.address(), socket_address.port_value());
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  Network::Utility::portFromTcpUrl(dns_url_);

  tls_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<PerThreadCurrentHostData>();
  });
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
      [this,
       dns_address](std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
        active_dns_query_ = nullptr;
        ENVOY_LOG(debug, "async DNS resolution complete for {}", dns_address);
        info_->stats().update_success_.inc();

        if (!address_list.empty()) {
          // TODO(mattklein123): Move port handling into the DNS interface.
          ASSERT(address_list.front() != nullptr);
          Network::Address::InstanceConstSharedPtr new_address =
              Network::Utility::getAddressWithPort(*address_list.front(),
                                                   Network::Utility::portFromTcpUrl(dns_url_));
          if (!current_resolved_address_ || !(*new_address == *current_resolved_address_)) {
            current_resolved_address_ = new_address;
            // Capture URL to avoid a race with another update.
            tls_->runOnAllThreads([this, new_address]() -> void {
              PerThreadCurrentHostData& data = tls_->getTyped<PerThreadCurrentHostData>();
              data.current_resolved_address_ = new_address;
            });
          }

          if (!logical_host_) {
            // TODO(mattklein123): The logical host is only used in /clusters admin output. We used
            // to show the friendly DNS name in that output, but currently there is no way to
            // express a DNS name inside of an Address::Instance. For now this is OK but we might
            // want to do better again later.
            switch (address_list.front()->ip()->version()) {
            case Network::Address::IpVersion::v4:
              logical_host_.reset(
                  new LogicalHost(info_, hostname_, Network::Utility::getIpv4AnyAddress(), *this));
              break;
            case Network::Address::IpVersion::v6:
              logical_host_.reset(
                  new LogicalHost(info_, hostname_, Network::Utility::getIpv6AnyAddress(), *this));
              break;
            }
            const auto& locality_lb_endpoint = localityLbEndpoint();
            PriorityStateManager priority_state_manager(*this, local_info_);
            priority_state_manager.initializePriorityFor(locality_lb_endpoint);
            priority_state_manager.registerHostForPriority(logical_host_, locality_lb_endpoint,
                                                           lbEndpoint(), absl::nullopt);

            const uint32_t priority = locality_lb_endpoint.priority();
            priority_state_manager.updateClusterPrioritySet(
                priority, std::move(priority_state_manager.priorityState()[priority].first),
                absl::nullopt, absl::nullopt, absl::nullopt);
          }
        }

        onPreInitComplete();
        resolve_timer_->enableTimer(dns_refresh_rate_ms_);
      });
}

Upstream::Host::CreateConnectionData LogicalDnsCluster::LogicalHost::createConnection(
    Event::Dispatcher& dispatcher,
    const Network::ConnectionSocket::OptionsSharedPtr& options) const {
  PerThreadCurrentHostData& data = parent_.tls_->getTyped<PerThreadCurrentHostData>();
  ASSERT(data.current_resolved_address_);
  return {HostImpl::createConnection(dispatcher, *parent_.info_, data.current_resolved_address_,
                                     options),
          HostDescriptionConstSharedPtr{
              new RealHostDescription(data.current_resolved_address_, parent_.localityLbEndpoint(),
                                      parent_.lbEndpoint(), shared_from_this())}};
}

} // namespace Upstream
} // namespace Envoy

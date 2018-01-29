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
                                     Network::DnsResolverSharedPtr dns_resolver,
                                     ThreadLocal::SlotAllocator& tls, ClusterManager& cm,
                                     Event::Dispatcher& dispatcher, bool added_via_api)
    : ClusterImplBase(cluster, cm.sourceAddress(), runtime, stats, ssl_context_manager,
                      added_via_api),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      tls_(tls.allocateSlot()),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })) {
  const auto& hosts = cluster.hosts();
  if (hosts.size() != 1) {
    throw EnvoyException("logical_dns clusters must have a single host");
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
    NOT_REACHED;
  }

  const auto& socket_address = hosts[0].socket_address();
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
              tls_->getTyped<PerThreadCurrentHostData>().current_resolved_address_ = new_address;
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
            HostVectorSharedPtr new_hosts(new std::vector<HostSharedPtr>());
            new_hosts->emplace_back(logical_host_);
            // Given the current config, only EDS clusters support multiple priorities.
            ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
            auto& first_host_set = priority_set_.getOrCreateHostSet(0);
            first_host_set.updateHosts(new_hosts, createHealthyHostList(*new_hosts),
                                       empty_host_lists_, empty_host_lists_, *new_hosts, {});
          }
        }

        onPreInitComplete();
        resolve_timer_->enableTimer(dns_refresh_rate_ms_);
      });
}

Upstream::Host::CreateConnectionData
LogicalDnsCluster::LogicalHost::createConnection(Event::Dispatcher& dispatcher) const {
  PerThreadCurrentHostData& data = parent_.tls_->getTyped<PerThreadCurrentHostData>();
  ASSERT(data.current_resolved_address_);
  return {HostImpl::createConnection(dispatcher, *parent_.info_, data.current_resolved_address_),
          HostDescriptionConstSharedPtr{
              new RealHostDescription(data.current_resolved_address_, shared_from_this())}};
}

} // namespace Upstream
} // namespace Envoy

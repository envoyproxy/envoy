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
    : ClusterImplBase(cluster, cm.bindConfig(), runtime, stats, ssl_context_manager, added_via_api),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      tls_(tls.allocateSlot()),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })),
      local_info_(local_info) {
  const auto& load_assignment = cluster.has_load_assignment()
                                    ? cluster.load_assignment()
                                    : Config::Utility::translateClusterHosts(cluster.hosts());
  const auto& endpoints = load_assignment.endpoints();
  if (endpoints.size() != 1 || endpoints[0].lb_endpoints().size() != 1) {
    throw EnvoyException("logical_dns clusters must have a single endpoint");
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

  const auto& locality_lb_endpoint = endpoints[0];
  const auto& lb_endpoint = locality_lb_endpoint.lb_endpoints()[0];

  const envoy::api::v2::endpoint::Endpoint& endpoint = lb_endpoint.endpoint();
  const envoy::api::v2::core::SocketAddress& socket_address = endpoint.address().socket_address();
  dns_url_ = fmt::format("tcp://{}:{}", socket_address.address(), socket_address.port_value());
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  Network::Utility::portFromTcpUrl(dns_url_);

  context_ = std::make_shared<ResolveTargetContext>(locality_lb_endpoint, lb_endpoint);

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
              data.context_ = context_;
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

            const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint =
                context_->locality_lb_endpoint();

            const uint32_t priority = locality_lb_endpoint.priority();
            if (priority_state_.size() <= priority) {
              priority_state_.resize(priority + 1);
            }

            if (priority_state_[priority].first == nullptr) {
              priority_state_[priority].first.reset(new HostVector());
            }

            if (locality_lb_endpoint.has_locality() &&
                locality_lb_endpoint.has_load_balancing_weight()) {
              priority_state_[priority].second[locality_lb_endpoint.locality()] =
                  locality_lb_endpoint.load_balancing_weight().value();
            }

            priority_state_[priority].first->emplace_back(logical_host_);
            initializePrioritySet(priority_set_, priority_state_, info(), local_info_, {}, {},
                                  false);
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
          HostDescriptionConstSharedPtr{new RealHostDescription(
              data.current_resolved_address_, data.context_, shared_from_this())}};
}

} // namespace Upstream
} // namespace Envoy

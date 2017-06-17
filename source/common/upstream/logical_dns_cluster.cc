#include "common/upstream/logical_dns_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Upstream {

LogicalDnsCluster::LogicalDnsCluster(const Json::Object& config, Runtime::Loader& runtime,
                                     Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                                     Network::DnsResolverSharedPtr dns_resolver,
                                     ThreadLocal::Instance& tls, Event::Dispatcher& dispatcher)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(config.getInteger("dns_refresh_rate_ms", 5000))),
      tls_(tls), tls_slot_(tls.allocateSlot()), initialized_(false),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })) {
  std::vector<Json::ObjectSharedPtr> hosts_json = config.getObjectArray("hosts");
  if (hosts_json.size() != 1) {
    throw EnvoyException("logical_dns clusters must have a single host");
  }

  std::string dns_lookup_family = config.getString("dns_lookup_family", "v4_only");
  if (dns_lookup_family == "v6_only") {
    dns_lookup_family_ = Network::DnsLookupFamily::V6Only;
  } else if (dns_lookup_family == "auto") {
    dns_lookup_family_ = Network::DnsLookupFamily::Auto;
  } else {
    ASSERT(dns_lookup_family == "v4_only");
    dns_lookup_family_ = Network::DnsLookupFamily::V4Only;
  }
  dns_url_ = hosts_json[0]->getString("url");
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  Network::Utility::portFromTcpUrl(dns_url_);

  // This must come before startResolve(), since the resolve callback relies on
  // tls_slot_ being initialized.
  tls.set(tls_slot_, [](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<PerThreadCurrentHostData>();
  });

  startResolve();
}

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
      [this, dns_address](
          std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
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
            tls_.runOnAllThreads([this, new_address]() -> void {
              tls_.getTyped<PerThreadCurrentHostData>(tls_slot_).current_resolved_address_ =
                  new_address;
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
            updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_lists_,
                        empty_host_lists_, *new_hosts, {});
          }
        }

        if (initialize_callback_) {
          initialize_callback_();
          initialize_callback_ = nullptr;
        }
        initialized_ = true;

        resolve_timer_->enableTimer(dns_refresh_rate_ms_);
      });
}

Upstream::Host::CreateConnectionData
LogicalDnsCluster::LogicalHost::createConnection(Event::Dispatcher& dispatcher) const {
  PerThreadCurrentHostData& data =
      parent_.tls_.getTyped<PerThreadCurrentHostData>(parent_.tls_slot_);
  ASSERT(data.current_resolved_address_);
  return {HostImpl::createConnection(dispatcher, *parent_.info_, data.current_resolved_address_),
          HostDescriptionConstSharedPtr{
              new RealHostDescription(data.current_resolved_address_, shared_from_this())}};
}

} // Upstream
} // Envoy

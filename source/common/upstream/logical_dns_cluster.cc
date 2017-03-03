#include "logical_dns_cluster.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Upstream {

LogicalDnsCluster::LogicalDnsCluster(const Json::Object& config, Runtime::Loader& runtime,
                                     Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                                     Network::DnsResolver& dns_resolver, ThreadLocal::Instance& tls,
                                     Event::Dispatcher& dispatcher)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(config.getInteger("dns_refresh_rate_ms", 5000))),
      tls_(tls), tls_slot_(tls.allocateSlot()), initialized_(false),
      resolve_timer_(dispatcher.createTimer([this]() -> void { startResolve(); })) {

  std::vector<Json::ObjectPtr> hosts_json = config.getObjectArray("hosts");
  if (hosts_json.size() != 1) {
    throw EnvoyException("logical_dns clusters must have a single host");
  }

  dns_url_ = hosts_json[0]->getString("url");
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  Network::Utility::portFromTcpUrl(dns_url_);

  // This must come before startResolve(), since the resolve callback relies on
  // tls_slot_ being initialized.
  tls.set(tls_slot_, [](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectPtr {
    return ThreadLocal::ThreadLocalObjectPtr{new PerThreadCurrentHostData()};
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
  log_debug("starting async DNS resolution for {}", dns_address);
  info_->stats().update_attempt_.inc();

  active_dns_query_ = dns_resolver_.resolve(
      dns_address,
      [this, dns_address](std::list<Network::Address::InstancePtr>&& address_list) -> void {
        active_dns_query_ = nullptr;
        log_debug("async DNS resolution complete for {}", dns_address);
        info_->stats().update_success_.inc();

        if (!address_list.empty()) {
          // TODO(mattklein123): IPv6 support as well as moving port handling into the DNS
          // interface.
          Network::Address::InstancePtr new_address(
              new Network::Address::Ipv4Instance(address_list.front()->ip()->addressAsString(),
                                                 Network::Utility::portFromTcpUrl(dns_url_)));
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
            logical_host_.reset(new LogicalHost(
                info_, hostname_, Network::Utility::resolveUrl("tcp://0.0.0.0:0"), *this));
            HostVectorPtr new_hosts(new std::vector<HostPtr>());
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
          HostDescriptionPtr{
              new RealHostDescription(data.current_resolved_address_, shared_from_this())}};
}

} // Upstream

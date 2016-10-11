#include "logical_dns_cluster.h"

#include "common/network/utility.h"

namespace Upstream {

LogicalDnsCluster::LogicalDnsCluster(const Json::Object& config, Runtime::Loader& runtime,
                                     Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                                     Network::DnsResolver& dns_resolver, ThreadLocal::Instance& tls)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager), dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(config.getInteger("dns_refresh_rate_ms", 5000))),
      tls_(tls), tls_slot_(tls.allocateSlot()),
      resolve_timer_(dns_resolver.dispatcher().createTimer([this]() -> void { startResolve(); })) {

  std::vector<Json::Object> hosts_json = config.getObjectArray("hosts");
  if (hosts_json.size() != 1) {
    throw EnvoyException("logical_dns clusters must have a single host");
  }

  dns_url_ = hosts_json[0].getString("url");
  Network::Utility::hostFromUrl(dns_url_);
  Network::Utility::portFromUrl(dns_url_);
  startResolve();

  tls.set(tls_slot_, [](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectPtr {
    return ThreadLocal::ThreadLocalObjectPtr{new PerThreadCurrentHostData()};
  });
}

void LogicalDnsCluster::startResolve() {
  std::string dns_address = Network::Utility::hostFromUrl(dns_url_);
  log_debug("starting async DNS resolution for {}", dns_address);
  stats_.update_attempt_.inc();

  dns_resolver_.resolve(
      dns_address, [this, dns_address](std::list<std::string>&& address_list) -> void {
        log_debug("async DNS resolution complete for {}", dns_address);
        stats_.update_success_.inc();

        if (!address_list.empty()) {
          std::string url = Network::Utility::urlForTcp(address_list.front(),
                                                        Network::Utility::portFromUrl(dns_url_));
          if (url != current_resolved_url_) {
            current_resolved_url_ = url;
            // Capture URL to avoid a race with another update.
            tls_.runOnAllThreads([this, url]() -> void {
              tls_.getTyped<PerThreadCurrentHostData>(tls_slot_).current_resolved_url_ = url;
            });
          }

          if (!logical_host_) {
            logical_host_.reset(new LogicalHost(*this, dns_url_, *this));
            HostVectorPtr new_hosts(new std::vector<HostPtr>());
            new_hosts->emplace_back(logical_host_);
            updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_list_,
                        empty_host_list_, *new_hosts, {});
          }
        }

        if (initialize_callback_) {
          initialize_callback_();
          initialize_callback_ = nullptr;
        }

        resolve_timer_->enableTimer(dns_refresh_rate_ms_);
      });
}

Upstream::Host::CreateConnectionData
LogicalDnsCluster::LogicalHost::createConnection(Event::Dispatcher& dispatcher) const {
  PerThreadCurrentHostData& data =
      parent_.tls_.getTyped<PerThreadCurrentHostData>(parent_.tls_slot_);
  ASSERT(!data.current_resolved_url_.empty());
  return {
      HostImpl::createConnection(dispatcher, parent_, data.current_resolved_url_),
      HostDescriptionPtr{new RealHostDescription(data.current_resolved_url_, shared_from_this())}};
}

} // Upstream

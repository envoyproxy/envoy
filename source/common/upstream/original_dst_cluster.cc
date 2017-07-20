#include "common/upstream/original_dst_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Upstream {

OriginalDstCluster::LoadBalancer::LoadBalancer(HostSet& host_set, OriginalDstCluster& parent)
    : host_set_(host_set), parent_(parent) {
  // host_set_ is initially empty.
  host_set_.addMemberUpdateCb([this](const std::vector<HostSharedPtr>& hosts_added,
                                     const std::vector<HostSharedPtr>& hosts_removed) -> void {
    // Update the hosts map
    for (auto host : hosts_removed) {
      ENVOY_LOG(debug, "REMOVING HOST {}", host->address()->asString());
      auto range = host_map_.equal_range(host->address()->asString());
      bool erased = false;
      for (auto it = range.first; it != range.second; ++it) {
        if (it->second == host) {
          host_map_.erase(it);
          erased = true;
          break; // erase invalidates 'it'
        }
      }
      if (!erased) {
        ENVOY_LOG(debug, "REMOVING HOST {}: Not Found!", host->address()->asString());
      }
    }
    for (auto host : hosts_added) {
      ENVOY_LOG(debug, "ADDING HOST {}", host->address()->asString());
      auto range = host_map_.equal_range(host->address()->asString());
      bool found = false;
      for (auto it = range.first; it != range.second; ++it) {
        if (it->second == host) {
          ENVOY_LOG(debug, "ADDING HOST {}: EXISTED ALREADY!", host->address()->asString());
          found = true;
          break;
        }
      }
      if (!found) {
        host_map_.insert({host->address()->asString(), host});
      }
    }
  });
}

HostConstSharedPtr
OriginalDstCluster::LoadBalancer::chooseHost(const LoadBalancerContext* context) {
  if (context) {
    auto connection = context->downstreamConnection();
    // Verify that we are not looping back to ourselves!
    if (connection && connection->usingOriginalDst()) {
      auto& localAddr = connection->localAddress();

      // Check if a host with the destination address is already in the host set.
      auto it = host_map_.find(localAddr.asString());

      if (it != host_map_.end()) {
        // Use the existing host
        auto host = it->second;
        ENVOY_LOG(debug, "FOUND HOST {}", host->address()->asString());
        host->weight(2); // Mark as used.
        return host;
      } else {
        // Add a new host
        auto localIP = localAddr.ip();
        if (localIP) {
          Network::Address::InstanceConstSharedPtr hostIpPort;
          if (localIP->version() == Network::Address::IpVersion::v4) {
            hostIpPort.reset(
                new Network::Address::Ipv4Instance(localIP->addressAsString(), localIP->port()));
          } else {
            hostIpPort.reset(
                new Network::Address::Ipv6Instance(localIP->addressAsString(), localIP->port()));
          }
          HostSharedPtr host = parent_.createHost(hostIpPort);

          ENVOY_LOG(debug, "CREATED HOST {}", host->address()->asString());
          // Add the new host to the map.  We just failed to find it in
          // our local map above, so just insert it.
          host_map_.insert({localAddr.asString(), host});
          host->weight(2); // Mark as used.
          return host;
        } else {
          ENVOY_LOG(debug, "FAILED TO CREATE HOST FOR {}", localAddr.asString());
        }
      }
    }
  }

  ENVOY_LOG(warn, "original_dst_load_balancer: No downstream connection or no original_dst.");
  return nullptr;
}

OriginalDstCluster::OriginalDstCluster(const Json::Object& config, Runtime::Loader& runtime,
                                       Stats::Store& stats,
                                       Ssl::ContextManager& ssl_context_manager,
                                       Event::Dispatcher& dispatcher, bool added_via_api)
    : ClusterImplBase(config, runtime, stats, ssl_context_manager, added_via_api),
      dispatcher_(dispatcher), cleanup_interval_ms_(std::chrono::milliseconds(
                                   config.getInteger("cleanup_interval_ms", 5000))),
      cleanup_timer_(dispatcher.createTimer([this]() -> void { cleanup(); })) {
  size_t n_hosts = 0;
  try {
    std::vector<Json::ObjectSharedPtr> hosts_json = config.getObjectArray("hosts");
    n_hosts = hosts_json.size();
  } catch (...) {
  } // Catch missing hosts config.
  if (n_hosts != 0) {
    throw EnvoyException("original_dst clusters must have no hosts configured");
  }

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

// Called by a worker thread.
HostSharedPtr OriginalDstCluster::createHost(Network::Address::InstanceConstSharedPtr address) {
  HostSharedPtr host(
      new HostImpl(info_, info_->name() + address->asString(), address, false, 1, ""));
  dispatcher_.post([this, host]() -> void { addHost(host); });
  return host;
}

// Called in the main thread.
void OriginalDstCluster::addHost(HostSharedPtr host) {
  HostVectorSharedPtr new_hosts(new std::vector<HostSharedPtr>(hosts()));
  new_hosts->emplace_back(host);
  updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_lists_, empty_host_lists_,
              {host}, {});
}

void OriginalDstCluster::cleanup() {
  ENVOY_LOG(debug, "CLEANUP.");
  HostVectorSharedPtr new_hosts(new std::vector<HostSharedPtr>);
  std::vector<HostSharedPtr> to_be_removed;
  auto hostset = hosts();
  for (auto host : hostset) {
    if (host->weight() == 1) {
      ENVOY_LOG(debug, "REMOVING {}.", host->address()->asString());
      to_be_removed.emplace_back(host);
    } else {
      ENVOY_LOG(debug, "KEEPING {}.", host->address()->asString());
      new_hosts->emplace_back(host);
      host->weight(1); // Mark to be removed next time.
    }
  }

  if (to_be_removed.size() > 0) {
    updateHosts(new_hosts, createHealthyHostList(*new_hosts), empty_host_lists_, empty_host_lists_,
                {}, to_be_removed);
  }

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

} // namespace Upstream
} // namespace Envoy

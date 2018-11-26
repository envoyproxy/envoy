#include "common/upstream/original_dst_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

// Static cast below is guaranteed to succeed, as code instantiating the cluster
// configuration, that is run prior to this code, checks that an OriginalDstCluster is
// always configured with an OriginalDstCluster::LoadBalancer, and that an
// OriginalDstCluster::LoadBalancer is never configured with any other type of cluster,
// and throws an exception otherwise.

OriginalDstCluster::LoadBalancer::LoadBalancer(
    PrioritySet& priority_set, ClusterSharedPtr& parent,
    const absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig>& config)
    : priority_set_(priority_set), parent_(std::static_pointer_cast<OriginalDstCluster>(parent)),
      info_(parent->info()), use_http_header_(config ? config.value().use_http_header() : false) {
  // priority_set_ is initially empty.
  priority_set_.addMemberUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
        // Update the hosts map
        // TODO(ramaraochavali): use cluster stats and move the log lines to debug.
        for (const HostSharedPtr& host : hosts_removed) {
          ENVOY_LOG(debug, "Removing host {}.", host->address()->asString());
          host_map_.remove(host);
        }
        for (const HostSharedPtr& host : hosts_added) {
          if (host_map_.insert(host)) {
            ENVOY_LOG(debug, "Adding host {}.", host->address()->asString());
          }
        }
      });
}

HostConstSharedPtr OriginalDstCluster::LoadBalancer::chooseHost(LoadBalancerContext* context) {
  if (context) {

    // Check if override host header is present, if yes use it otherwise check local address.
    Network::Address::InstanceConstSharedPtr dst_host = nullptr;
    if (use_http_header_) {
      dst_host = requestOverrideHost(context);
    }
    if (dst_host == nullptr) {
      const Network::Connection* connection = context->downstreamConnection();
      // The local address of the downstream connection is the original destination address,
      // if localAddressRestored() returns 'true'.
      if (connection && connection->localAddressRestored()) {
        dst_host = connection->localAddress();
      }
    }

    if (dst_host) {
      const Network::Address::Instance& dst_addr = *dst_host.get();

      // Check if a host with the destination address is already in the host set.
      HostSharedPtr host = host_map_.find(dst_addr);
      if (host) {
        ENVOY_LOG(debug, "Using existing host {}.", host->address()->asString());
        host->used(true); // Mark as used.
        return std::move(host);
      }
      // Add a new host
      const Network::Address::Ip* dst_ip = dst_addr.ip();
      if (dst_ip) {
        Network::Address::InstanceConstSharedPtr host_ip_port(
            Network::Utility::copyInternetAddressAndPort(*dst_ip));
        // Create a host we can use immediately.
        host.reset(new HostImpl(
            info_, info_->name() + dst_addr.asString(), std::move(host_ip_port),
            envoy::api::v2::core::Metadata::default_instance(), 1,
            envoy::api::v2::core::Locality().default_instance(),
            envoy::api::v2::endpoint::Endpoint::HealthCheckConfig().default_instance(), 0));

        ENVOY_LOG(debug, "Created host {}.", host->address()->asString());
        // Add the new host to the map. We just failed to find it in
        // our local map above, so insert without checking (2nd arg == false).
        host_map_.insert(host, false);

        if (std::shared_ptr<OriginalDstCluster> parent = parent_.lock()) {
          // lambda cannot capture a member by value.
          std::weak_ptr<OriginalDstCluster> post_parent = parent_;
          parent->dispatcher_.post([post_parent, host]() mutable {
            // The main cluster may have disappeared while this post was queued.
            if (std::shared_ptr<OriginalDstCluster> parent = post_parent.lock()) {
              parent->addHost(host);
            }
          });
        }

        return std::move(host);
      } else {
        ENVOY_LOG(debug, "Failed to create host for {}.", dst_addr.asString());
      }
    }
  }
  // TODO(ramaraochavali): add a stat and move this log line to debug.
  ENVOY_LOG(warn, "original_dst_load_balancer: No downstream connection or no original_dst.");
  return nullptr;
}

Network::Address::InstanceConstSharedPtr
OriginalDstCluster::LoadBalancer::requestOverrideHost(LoadBalancerContext* context) {
  Network::Address::InstanceConstSharedPtr request_host;
  const Http::HeaderMap* downstream_headers = context->downstreamHeaders();
  if (downstream_headers &&
      downstream_headers->get(Http::Headers::get().EnvoyOriginalDstHost) != nullptr) {
    const std::string& request_override_host =
        downstream_headers->get(Http::Headers::get().EnvoyOriginalDstHost)->value().c_str();
    try {
      request_host = Network::Utility::parseInternetAddressAndPort(request_override_host, false);
      ENVOY_LOG(debug, "Using request override host {}.", request_override_host);
    } catch (const Envoy::EnvoyException& e) {
      ENVOY_LOG(debug, "original_dst_load_balancer: invalid override header value. {}", e.what());
      info_->stats().original_dst_host_invalid_.inc();
    }
  }
  return request_host;
}

OriginalDstCluster::OriginalDstCluster(
    const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(config, runtime, factory_context, std::move(stats_scope), added_via_api),
      dispatcher_(factory_context.dispatcher()),
      cleanup_interval_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, cleanup_interval, 5000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })) {

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

void OriginalDstCluster::addHost(HostSharedPtr& host) {
  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  HostVectorSharedPtr new_hosts(new HostVector(first_host_set.hosts()));
  new_hosts->emplace_back(host);
  first_host_set.updateHosts(new_hosts, createHealthyHostList(*new_hosts),
                             HostsPerLocalityImpl::empty(), HostsPerLocalityImpl::empty(), {},
                             {std::move(host)}, {}, absl::nullopt);
}

void OriginalDstCluster::cleanup() {
  HostVectorSharedPtr new_hosts(new HostVector);
  HostVector to_be_removed;
  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);
  auto& host_set = priority_set_.getOrCreateHostSet(0);

  ENVOY_LOG(debug, "Cleaning up stale original dst hosts.");
  for (const HostSharedPtr& host : host_set.hosts()) {
    if (host->used()) {
      ENVOY_LOG(debug, "Keeping active host {}.", host->address()->asString());
      new_hosts->emplace_back(host);
      host->used(false); // Mark to be removed during the next round.
    } else {
      ENVOY_LOG(debug, "Removing stale host {}.", host->address()->asString());
      to_be_removed.emplace_back(host);
    }
  }

  if (to_be_removed.size() > 0) {
    host_set.updateHosts(new_hosts, createHealthyHostList(*new_hosts),
                         HostsPerLocalityImpl::empty(), HostsPerLocalityImpl::empty(), {}, {},
                         to_be_removed, absl::nullopt);
  }

  cleanup_timer_->enableTimer(cleanup_interval_ms_);
}

} // namespace Upstream
} // namespace Envoy

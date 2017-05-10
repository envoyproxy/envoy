#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/upstream/upstream_impl.h"

namespace Lyft {
namespace Upstream {

/**
 * The LogicalDnsCluster is a type of cluster that creates a single logical host that wraps
 * an async DNS resolver. The DNS resolver will continuously resolve, and swap in the first IP
 * address in the resolution set. However the logical owning host will not change. Any created
 * connections against this host will use the currently resolved IP. This means that a connection
 * pool using the logical host may end up with connections to many different real IPs.
 *
 * This cluster type is useful for large web services that use DNS in a round robin fashion, such
 * that DNS resolution may repeatedly return different results. A single connection pool can be
 * created that will internally have connections to different backends, while still allowing long
 * connection lengths and keep alive. The cluster type should only be used when an IP address change
 * means that connections using the IP should not drain.
 */
class LogicalDnsCluster : public ClusterImplBase {
public:
  LogicalDnsCluster(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager, Network::DnsResolver& dns_resolver,
                    ThreadLocal::Instance& tls, Event::Dispatcher& dispatcher);

  ~LogicalDnsCluster();

  // Upstream::Cluster
  void initialize() override {}
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  void setInitializedCb(std::function<void()> callback) override {
    if (initialized_) {
      callback();
    } else {
      initialize_callback_ = callback;
    }
  }

private:
  struct LogicalHost : public HostImpl {
    LogicalHost(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
                Network::Address::InstanceConstSharedPtr address, LogicalDnsCluster& parent)
        : HostImpl(cluster, hostname, address, false, 1, ""), parent_(parent) {}

    // Upstream::Host
    CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override;

    LogicalDnsCluster& parent_;
  };

  struct RealHostDescription : public HostDescription {
    RealHostDescription(Network::Address::InstanceConstSharedPtr address,
                        HostConstSharedPtr logical_host)
        : address_(address), logical_host_(logical_host) {}

    // Upstream:HostDescription
    bool canary() const override { return false; }
    const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
    Outlier::DetectorHostSink& outlierDetector() const override {
      return logical_host_->outlierDetector();
    }
    const HostStats& stats() const override { return logical_host_->stats(); }
    const std::string& hostname() const override { return logical_host_->hostname(); }
    Network::Address::InstanceConstSharedPtr address() const override { return address_; }
    const std::string& zone() const override { return EMPTY_STRING; }

    Network::Address::InstanceConstSharedPtr address_;
    HostConstSharedPtr logical_host_;
  };

  struct PerThreadCurrentHostData : public ThreadLocal::ThreadLocalObject {
    // ThreadLocal::ThreadLocalObject
    void shutdown() override {}

    Network::Address::InstanceConstSharedPtr current_resolved_address_;
  };

  void startResolve();

  Network::DnsResolver& dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  std::function<void()> initialize_callback_;
  // Set once the first resolve completes.
  bool initialized_;
  Event::TimerPtr resolve_timer_;
  std::string dns_url_;
  std::string hostname_;
  Network::Address::InstanceConstSharedPtr current_resolved_address_;
  HostSharedPtr logical_host_;
  Network::ActiveDnsQuery* active_dns_query_{};
};

} // Upstream
} // Lyft
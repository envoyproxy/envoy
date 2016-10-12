#pragma once

#include "upstream_impl.h"

#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"

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
                    ThreadLocal::Instance& tls);

  // Upstream::Cluster
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }
  void shutdown() override {}

private:
  struct LogicalHost : public HostImpl {
    LogicalHost(const Cluster& cluster, const std::string& url, LogicalDnsCluster& parent)
        : HostImpl(cluster, url, false, 1, ""), parent_(parent) {}

    // Upstream::Host
    CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override;

    LogicalDnsCluster& parent_;
  };

  struct RealHostDescription : public HostDescription {
    RealHostDescription(const std::string& url, ConstHostPtr logical_host)
        : url_(url), logical_host_(logical_host) {}

    // Upstream:HostDescription
    bool canary() const override { return false; }
    const Cluster& cluster() const override { return logical_host_->cluster(); }
    OutlierDetectorHostSink& outlierDetector() const override {
      return logical_host_->outlierDetector();
    }
    const HostStats& stats() const override { return logical_host_->stats(); }
    const std::string& url() const override { return url_; }
    const std::string& zone() const override { return EMPTY_STRING; }

    const std::string url_;
    ConstHostPtr logical_host_;
  };

  struct PerThreadCurrentHostData : public ThreadLocal::ThreadLocalObject {
    // ThreadLocal::ThreadLocalObject
    void shutdown() override {}

    std::string current_resolved_url_;
  };

  void startResolve();

  Network::DnsResolver& dns_resolver_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  std::function<void()> initialize_callback_;
  Event::TimerPtr resolve_timer_;
  std::string dns_url_;
  std::string current_resolved_url_;
  HostPtr logical_host_;
};

} // Upstream

#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/socket_impl.h"
#include "common/network/socket_interface.h"
#include "common/network/utility.h"
#include "common/upstream/load_balancer_impl.h"

#include "extensions/filters/udp/udp_proxy/hash_policy_impl.h"

#include "absl/container/flat_hash_set.h"

// TODO(mattklein123): UDP session access logging.

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

/**
 * All UDP proxy downstream stats. @see stats_macros.h
 */
#define ALL_UDP_PROXY_DOWNSTREAM_STATS(COUNTER, GAUGE)                                             \
  COUNTER(downstream_sess_no_route)                                                                \
  COUNTER(downstream_sess_rx_bytes)                                                                \
  COUNTER(downstream_sess_rx_datagrams)                                                            \
  COUNTER(downstream_sess_rx_errors)                                                               \
  COUNTER(downstream_sess_total)                                                                   \
  COUNTER(downstream_sess_tx_bytes)                                                                \
  COUNTER(downstream_sess_tx_datagrams)                                                            \
  COUNTER(downstream_sess_tx_errors)                                                               \
  COUNTER(idle_timeout)                                                                            \
  GAUGE(downstream_sess_active, Accumulate)

/**
 * Struct definition for all UDP proxy downstream stats. @see stats_macros.h
 */
struct UdpProxyDownstreamStats {
  ALL_UDP_PROXY_DOWNSTREAM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * All UDP proxy upstream cluster stats. @see stats_macros.h
 */
#define ALL_UDP_PROXY_UPSTREAM_STATS(COUNTER)                                                      \
  COUNTER(sess_rx_datagrams)                                                                       \
  COUNTER(sess_rx_errors)                                                                          \
  COUNTER(sess_tx_datagrams)                                                                       \
  COUNTER(sess_tx_errors)

/**
 * Struct definition for all UDP proxy upstream stats. @see stats_macros.h
 */
struct UdpProxyUpstreamStats {
  ALL_UDP_PROXY_UPSTREAM_STATS(GENERATE_COUNTER_STRUCT)
};

class UdpProxyFilterConfig {
public:
  UdpProxyFilterConfig(Upstream::ClusterManager& cluster_manager, TimeSource& time_source,
                       Stats::Scope& root_scope,
                       const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config)
      : cluster_manager_(cluster_manager), time_source_(time_source), cluster_(config.cluster()),
        session_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, idle_timeout, 60 * 1000)),
        use_original_src_ip_(config.use_original_src_ip()),
        stats_(generateStats(config.stat_prefix(), root_scope)) {
    if (use_original_src_ip_ && !Api::OsSysCallsSingleton::get().supportsIpTransparent()) {
      ExceptionUtil::throwEnvoyException(
          "The platform does not support either IP_TRANSPARENT or IPV6_TRANSPARENT. Or the envoy "
          "is not running with the CAP_NET_ADMIN capability.");
    }
    if (!config.hash_policies().empty()) {
      hash_policy_ = std::make_unique<HashPolicyImpl>(config.hash_policies());
    }
  }

  const std::string& cluster() const { return cluster_; }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  std::chrono::milliseconds sessionTimeout() const { return session_timeout_; }
  bool usingOriginalSrcIp() const { return use_original_src_ip_; }
  const Udp::HashPolicy* hashPolicy() const { return hash_policy_.get(); }
  UdpProxyDownstreamStats& stats() const { return stats_; }
  TimeSource& timeSource() const { return time_source_; }

private:
  static UdpProxyDownstreamStats generateStats(const std::string& stat_prefix,
                                               Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("udp.", stat_prefix);
    return {ALL_UDP_PROXY_DOWNSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                           POOL_GAUGE_PREFIX(scope, final_prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
  const std::string cluster_;
  const std::chrono::milliseconds session_timeout_;
  const bool use_original_src_ip_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  mutable UdpProxyDownstreamStats stats_;
};

using UdpProxyFilterConfigSharedPtr = std::shared_ptr<const UdpProxyFilterConfig>;

/**
 * Currently, it only implements the hash based routing.
 */
class UdpLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  UdpLoadBalancerContext(const Udp::HashPolicy* hash_policy,
                         const Network::Address::InstanceConstSharedPtr& peer_address) {
    if (hash_policy) {
      hash_ = hash_policy->generateHash(*peer_address);
    }
  }

  absl::optional<uint64_t> computeHashKey() override { return hash_; }

private:
  absl::optional<uint64_t> hash_;
};

class UdpProxyFilter : public Network::UdpListenerReadFilter,
                       public Upstream::ClusterUpdateCallbacks,
                       Logger::Loggable<Logger::Id::filter> {
public:
  UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                 const UdpProxyFilterConfigSharedPtr& config);

  // Network::UdpListenerReadFilter
  void onData(Network::UdpRecvData& data) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;

private:
  class ClusterInfo;

  /**
   * An active session is similar to a TCP connection. It binds a 4-tuple (downstream IP/port, local
   * IP/port) to a selected upstream host for the purpose of packet forwarding. Unlike a TCP
   * connection, there is obviously no concept of session destruction beyond internally tracked data
   * such as an idle timeout, maximum packets, etc. Once a session is created, downstream packets
   * will be hashed to the same session and will be forwarded to the same upstream, using the same
   * local ephemeral IP/port.
   */
  class ActiveSession : public Network::UdpPacketProcessor {
  public:
    ActiveSession(ClusterInfo& parent, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                  const Upstream::HostConstSharedPtr& host);
    ~ActiveSession() override;
    const Network::UdpRecvData::LocalPeerAddresses& addresses() const { return addresses_; }
    const Upstream::Host& host() const { return *host_; }
    void write(const Buffer::Instance& buffer);

  private:
    void onIdleTimer();
    void onReadReady();

    // Network::UdpPacketProcessor
    void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                       Network::Address::InstanceConstSharedPtr peer_address,
                       Buffer::InstancePtr buffer, MonotonicTime receive_time) override;
    uint64_t maxPacketSize() const override {
      // TODO(mattklein123): Support configurable/jumbo frames when proxying to upstream.
      // Eventually we will want to support some type of PROXY header when doing L4 QUIC
      // forwarding.
      return Network::MAX_UDP_PACKET_SIZE;
    }

    ClusterInfo& cluster_;
    const bool use_original_src_ip_;
    const Network::UdpRecvData::LocalPeerAddresses addresses_;
    const Upstream::HostConstSharedPtr host_;
    // TODO(mattklein123): Consider replacing an idle timer for each session with a last used
    // time stamp and a periodic scan of all sessions to look for timeouts. This solution is simple,
    // though it might not perform well for high volume traffic. Note that this is how TCP proxy
    // idle timeouts work so we should consider unifying the implementation if we move to a time
    // stamp and scan approach.
    const Event::TimerPtr idle_timer_;
    // The socket is used for writing packets to the selected upstream host as well as receiving
    // packets from the upstream host. Note that a a local ephemeral port is bound on the first
    // write to the upstream host.
    const Network::SocketPtr socket_;
  };

  using ActiveSessionPtr = std::unique_ptr<ActiveSession>;

  struct HeterogeneousActiveSessionHash {
    // Specifying is_transparent indicates to the library infrastructure that
    // type-conversions should not be applied when calling find(), but instead
    // pass the actual types of the contained and searched-for objects directly to
    // these functors. See
    // https://en.cppreference.com/w/cpp/utility/functional/less_void for an
    // official reference, and https://abseil.io/tips/144 for a description of
    // using it in the context of absl.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    size_t operator()(const Network::UdpRecvData::LocalPeerAddresses& value) const {
      return absl::Hash<const Network::UdpRecvData::LocalPeerAddresses>()(value);
    }
    size_t operator()(const ActiveSessionPtr& value) const {
      return absl::Hash<const Network::UdpRecvData::LocalPeerAddresses>()(value->addresses());
    }
    size_t operator()(const ActiveSession* value) const {
      return absl::Hash<const Network::UdpRecvData::LocalPeerAddresses>()(value->addresses());
    }
  };

  struct HeterogeneousActiveSessionEqual {
    // See description for HeterogeneousActiveSessionHash::is_transparent.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    bool operator()(const ActiveSessionPtr& lhs,
                    const Network::UdpRecvData::LocalPeerAddresses& rhs) const {
      return lhs->addresses() == rhs;
    }
    bool operator()(const ActiveSessionPtr& lhs, const ActiveSessionPtr& rhs) const {
      return lhs->addresses() == rhs->addresses();
    }
    bool operator()(const ActiveSessionPtr& lhs, const ActiveSession* rhs) const {
      return lhs->addresses() == rhs->addresses();
    }
  };

  /**
   * Wraps all cluster specific UDP processing including session tracking, stats, etc. In the future
   * we will very likely support different types of routing to multiple upstream clusters.
   */
  class ClusterInfo {
  public:
    ClusterInfo(UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster);
    ~ClusterInfo();
    void onData(Network::UdpRecvData& data);
    void removeSession(const ActiveSession* session);

    UdpProxyFilter& filter_;
    Upstream::ThreadLocalCluster& cluster_;
    UdpProxyUpstreamStats cluster_stats_;

  private:
    ActiveSession* createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                 const Upstream::HostConstSharedPtr& host);
    static UdpProxyUpstreamStats generateStats(Stats::Scope& scope) {
      const auto final_prefix = "udp";
      return {ALL_UDP_PROXY_UPSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
    }

    Envoy::Common::CallbackHandle* member_update_cb_handle_;
    absl::flat_hash_set<ActiveSessionPtr, HeterogeneousActiveSessionHash,
                        HeterogeneousActiveSessionEqual>
        sessions_;
    absl::flat_hash_map<const Upstream::Host*, absl::flat_hash_set<const ActiveSession*>>
        host_to_sessions_;
  };

  virtual Network::SocketPtr createSocket(const Upstream::HostConstSharedPtr& host) {
    // Virtual so this can be overridden in unit tests.
    return std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, host->address());
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) final;
  void onClusterRemoval(const std::string& cluster_name) override;

  const UdpProxyFilterConfigSharedPtr config_;
  const Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_;
  // Right now we support a single cluster to route to. It is highly likely in the future that
  // we will support additional routing options either using filter chain matching, weighting,
  // etc.
  absl::optional<ClusterInfo> cluster_info_;
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

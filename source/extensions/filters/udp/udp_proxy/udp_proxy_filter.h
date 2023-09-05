#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/network/filter.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/extensions/filters/udp/udp_proxy/hash_policy_impl.h"
#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

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
  COUNTER(sess_rx_datagrams_dropped)                                                               \
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
  virtual ~UdpProxyFilterConfig() = default;

  virtual const std::string route(const Network::Address::Instance& destination_address,
                                  const Network::Address::Instance& source_address) const PURE;
  virtual const std::vector<std::string>& allClusterNames() const PURE;
  virtual Upstream::ClusterManager& clusterManager() const PURE;
  virtual std::chrono::milliseconds sessionTimeout() const PURE;
  virtual bool usingOriginalSrcIp() const PURE;
  virtual bool usingPerPacketLoadBalancing() const PURE;
  virtual const Udp::HashPolicy* hashPolicy() const PURE;
  virtual UdpProxyDownstreamStats& stats() const PURE;
  virtual TimeSource& timeSource() const PURE;
  virtual Random::RandomGenerator& randomGenerator() const PURE;
  virtual const Network::ResolvedUdpSocketConfig& upstreamSocketConfig() const PURE;
  virtual const std::vector<AccessLog::InstanceSharedPtr>& sessionAccessLogs() const PURE;
  virtual const std::vector<AccessLog::InstanceSharedPtr>& proxyAccessLogs() const PURE;
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
  ~UdpProxyFilter() override;

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

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
    void fillSessionStreamInfo();

    // Network::UdpPacketProcessor
    void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                       Network::Address::InstanceConstSharedPtr peer_address,
                       Buffer::InstancePtr buffer, MonotonicTime receive_time) override;
    uint64_t maxDatagramSize() const override {
      return cluster_.filter_.config_->upstreamSocketConfig().max_rx_datagram_size_;
    }
    void onDatagramsDropped(uint32_t dropped) override {
      cluster_.cluster_stats_.sess_rx_datagrams_dropped_.add(dropped);
    }
    size_t numPacketsExpectedPerEventLoop() const final {
      // TODO(mattklein123) change this to a reasonable number if needed.
      return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
    }

    /**
     * Struct definition for session access logging.
     */
    struct UdpProxySessionStats {
      uint64_t downstream_sess_tx_bytes_;
      uint64_t downstream_sess_rx_bytes_;
      uint64_t downstream_sess_tx_errors_;
      uint64_t downstream_sess_rx_errors_;
      uint64_t downstream_sess_tx_datagrams_;
      uint64_t downstream_sess_rx_datagrams_;
    };

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
    // The socket has been connected to avoid port exhaustion.
    bool connected_{};

    UdpProxySessionStats session_stats_{};
    StreamInfo::StreamInfoImpl udp_session_info_;
  };

  using ActiveSessionPtr = std::unique_ptr<ActiveSession>;

  struct LocalPeerHostAddresses {
    const Network::UdpRecvData::LocalPeerAddresses& local_peer_addresses_;
    const Upstream::Host& host_;
  };

  struct HeterogeneousActiveSessionHash {
    // Specifying is_transparent indicates to the library infrastructure that
    // type-conversions should not be applied when calling find(), but instead
    // pass the actual types of the contained and searched-for objects directly to
    // these functors. See
    // https://en.cppreference.com/w/cpp/utility/functional/less_void for an
    // official reference, and https://abseil.io/tips/144 for a description of
    // using it in the context of absl.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    HeterogeneousActiveSessionHash(const bool consider_host) : consider_host_(consider_host) {}

    size_t operator()(const Network::UdpRecvData::LocalPeerAddresses& value) const {
      return absl::Hash<const Network::UdpRecvData::LocalPeerAddresses>()(value);
    }
    size_t operator()(const LocalPeerHostAddresses& value) const {
      auto hash = this->operator()(value.local_peer_addresses_);
      if (consider_host_) {
        hash = absl::HashOf(hash, value.host_.address()->asStringView());
      }
      return hash;
    }
    size_t operator()(const ActiveSession* value) const {
      LocalPeerHostAddresses key{value->addresses(), value->host()};
      return this->operator()(key);
    }
    size_t operator()(const ActiveSessionPtr& value) const { return this->operator()(value.get()); }

  private:
    const bool consider_host_;
  };

  struct HeterogeneousActiveSessionEqual {
    // See description for HeterogeneousActiveSessionHash::is_transparent.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    HeterogeneousActiveSessionEqual(const bool consider_host) : consider_host_(consider_host) {}

    bool operator()(const ActiveSessionPtr& lhs,
                    const Network::UdpRecvData::LocalPeerAddresses& rhs) const {
      return lhs->addresses() == rhs;
    }
    bool operator()(const ActiveSessionPtr& lhs, const LocalPeerHostAddresses& rhs) const {
      return this->operator()(lhs, rhs.local_peer_addresses_) &&
             (consider_host_ ? &lhs->host() == &rhs.host_ : true);
    }
    bool operator()(const ActiveSessionPtr& lhs, const ActiveSession* rhs) const {
      LocalPeerHostAddresses key{rhs->addresses(), rhs->host()};
      return this->operator()(lhs, key);
    }
    bool operator()(const ActiveSessionPtr& lhs, const ActiveSessionPtr& rhs) const {
      return this->operator()(lhs, rhs.get());
    }

  private:
    const bool consider_host_;
  };

  /**
   * Wraps all cluster specific UDP processing including session tracking, stats, etc. In the future
   * we will very likely support different types of routing to multiple upstream clusters.
   */
  class ClusterInfo {
  protected:
    using SessionStorageType = absl::flat_hash_set<ActiveSessionPtr, HeterogeneousActiveSessionHash,
                                                   HeterogeneousActiveSessionEqual>;

  public:
    ClusterInfo(UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster,
                SessionStorageType&& sessions);
    virtual ~ClusterInfo();
    virtual Network::FilterStatus onData(Network::UdpRecvData& data) PURE;
    void removeSession(const ActiveSession* session);

    UdpProxyFilter& filter_;
    Upstream::ThreadLocalCluster& cluster_;
    UdpProxyUpstreamStats cluster_stats_;

  protected:
    ActiveSession* createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                 const Upstream::HostConstSharedPtr& optional_host = nullptr);
    Upstream::HostConstSharedPtr
    chooseHost(const Network::Address::InstanceConstSharedPtr& peer_address) const;

    SessionStorageType sessions_;

  private:
    static UdpProxyUpstreamStats generateStats(Stats::Scope& scope) {
      const auto final_prefix = "udp";
      return {ALL_UDP_PROXY_UPSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
    }
    ActiveSession* createSessionWithHost(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                         const Upstream::HostConstSharedPtr& host);

    Envoy::Common::CallbackHandlePtr member_update_cb_handle_;
    absl::flat_hash_map<const Upstream::Host*, absl::flat_hash_set<const ActiveSession*>>
        host_to_sessions_;
  };

  using ClusterInfoPtr = std::unique_ptr<ClusterInfo>;

  /**
   * Performs forwarding and replying data to one upstream host, selected when the first datagram
   * for a session is received. If the upstream host becomes unhealthy, a new one is selected.
   */
  class StickySessionClusterInfo : public ClusterInfo {
  public:
    StickySessionClusterInfo(UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster);
    Network::FilterStatus onData(Network::UdpRecvData& data) override;
  };

  /**
   * On each data chunk selects another host using underlying load balancing method and communicates
   * with that host.
   */
  class PerPacketLoadBalancingClusterInfo : public ClusterInfo {
  public:
    PerPacketLoadBalancingClusterInfo(UdpProxyFilter& filter,
                                      Upstream::ThreadLocalCluster& cluster);
    Network::FilterStatus onData(Network::UdpRecvData& data) override;
  };

  virtual Network::SocketPtr createSocket(const Upstream::HostConstSharedPtr& host) {
    // Virtual so this can be overridden in unit tests.
    return std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, host->address(),
                                                 nullptr, Network::SocketCreationOptions{});
  }

  void fillProxyStreamInfo();

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) final;
  void onClusterRemoval(const std::string& cluster_name) override;

  const UdpProxyFilterConfigSharedPtr config_;
  const Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_;
  // Map for looking up cluster info with its name.
  absl::flat_hash_map<std::string, ClusterInfoPtr> cluster_infos_;

  absl::optional<StreamInfo::StreamInfoImpl> udp_proxy_stats_;
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

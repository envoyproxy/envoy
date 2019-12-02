#pragma once

#include "envoy/config/filter/udp/udp_proxy/v2alpha/udp_proxy.pb.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/network/utility.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

/**
 * All UDP proxy stats. @see stats_macros.h
 */
#define ALL_UDP_PROXY_STATS(COUNTER, GAUGE)                                                        \
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
 * Struct definition for all UDP proxy stats. @see stats_macros.h
 */
struct UdpProxyStats {
  ALL_UDP_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class UdpProxyFilterConfig {
public:
  UdpProxyFilterConfig(Upstream::ClusterManager& cluster_manager, TimeSource& time_source,
                       Stats::Scope& root_scope,
                       const envoy::config::filter::udp::udp_proxy::v2alpha::UdpProxyConfig& config)
      : cluster_manager_(cluster_manager), time_source_(time_source), cluster_(config.cluster()),
        session_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, idle_timeout, 60 * 1000)),
        stats_(generateStats(config.stat_prefix(), root_scope)) {}

  Upstream::ThreadLocalCluster* getCluster() const { return cluster_manager_.get(cluster_); }
  std::chrono::milliseconds sessionTimeout() const { return session_timeout_; }
  UdpProxyStats& stats() const { return stats_; }
  TimeSource& timeSource() const { return time_source_; }

private:
  static UdpProxyStats generateStats(const std::string& stat_prefix, Stats::Scope& scope) {
    const auto final_prefix = fmt::format("udp.{}", stat_prefix);
    return {ALL_UDP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                POOL_GAUGE_PREFIX(scope, final_prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
  const std::string cluster_;
  const std::chrono::milliseconds session_timeout_;
  mutable UdpProxyStats stats_;
};

using UdpProxyFilterConfigSharedPtr = std::shared_ptr<const UdpProxyFilterConfig>;

class UdpProxyFilter : public Network::UdpListenerReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                 const UdpProxyFilterConfigSharedPtr& config)
      : UdpListenerReadFilter(callbacks), config_(config) {}

  // Network::UdpListenerReadFilter
  void onData(Network::UdpRecvData& data) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;

private:
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
    ActiveSession(UdpProxyFilter& parent, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                  const Upstream::HostConstSharedPtr& host);
    ~ActiveSession();
    const Network::UdpRecvData::LocalPeerAddresses& addresses() { return addresses_; }
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

    UdpProxyFilter& parent_;
    const Network::UdpRecvData::LocalPeerAddresses addresses_;
    const Upstream::HostConstSharedPtr host_;
    // TODO(mattklein123): Consider replacing an idle timer for each session with a last used
    // time stamp and a periodic scan of all sessions to look for timeouts. This solution is simple,
    // though it might not perform well for high volume traffic. Note that this is how TCP proxy
    // idle timeouts work so we should consider unifying the implementation if we move to a time
    // stamp and scan approach.
    const Event::TimerPtr idle_timer_;
    // The IO handle is used for writing packets to the selected upstream host as well as receiving
    // packets from the upstream host. Note that a a local ephemeral port is bound on the first
    // write to the upstream host.
    const Network::IoHandlePtr io_handle_;
    const Event::FileEventPtr socket_event_;
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
  };

  virtual Network::IoHandlePtr createIoHandle(const Upstream::HostConstSharedPtr& host) {
    // Virtual so this can be overridden in unit tests.
    return host->address()->socket(Network::Address::SocketType::Datagram);
  }

  const UdpProxyFilterConfigSharedPtr config_;
  absl::flat_hash_set<ActiveSessionPtr, HeterogeneousActiveSessionHash,
                      HeterogeneousActiveSessionEqual>
      sessions_;
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

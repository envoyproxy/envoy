#pragma once

#include <queue>

#include "envoy/access_log/access_log.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/network/filter.h"
#include "envoy/stream_info/uint32_accessor.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/filters/udp/udp_proxy/hash_policy_impl.h"
#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"
#include "source/extensions/filters/udp/udp_proxy/tunneling_connection_pool.h"

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
  COUNTER(session_filter_config_missing)                                                           \
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
  COUNTER(sess_tunnel_success)                                                                     \
  COUNTER(sess_tunnel_failure)                                                                     \
  COUNTER(sess_tunnel_buffer_overflow)                                                             \
  COUNTER(sess_tx_errors)

/**
 * Struct definition for all UDP proxy upstream stats. @see stats_macros.h
 */
struct UdpProxyUpstreamStats {
  ALL_UDP_PROXY_UPSTREAM_STATS(GENERATE_COUNTER_STRUCT)
};

using UdpSessionFilterChainFactory = Network::UdpSessionFilterChainFactory;

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
  virtual const Network::ResolvedUdpSocketConfig& upstreamSocketConfig() const PURE;
  virtual const AccessLog::InstanceSharedPtrVector& sessionAccessLogs() const PURE;
  virtual const AccessLog::InstanceSharedPtrVector& proxyAccessLogs() const PURE;
  virtual const UdpSessionFilterChainFactory& sessionFilterFactory() const PURE;
  virtual bool hasSessionFilters() const PURE;
  virtual const UdpTunnelingConfigPtr& tunnelingConfig() const PURE;
  virtual bool flushAccessLogOnTunnelConnected() const PURE;
  virtual const absl::optional<std::chrono::milliseconds>& accessLogFlushInterval() const PURE;
  virtual Random::RandomGenerator& randomGenerator() const PURE;
};

using UdpProxyFilterConfigSharedPtr = std::shared_ptr<const UdpProxyFilterConfig>;

/**
 * Currently, it only implements the hash based routing.
 */
class UdpLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  UdpLoadBalancerContext(const Udp::HashPolicy* hash_policy,
                         const Network::Address::InstanceConstSharedPtr& peer_address,
                         StreamInfo::StreamInfo* stream_info)
      : stream_info_(stream_info) {
    if (hash_policy) {
      hash_ = hash_policy->generateHash(*peer_address);
    }
  }

  absl::optional<uint64_t> computeHashKey() override { return hash_; }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return stream_info_; }

private:
  absl::optional<uint64_t> hash_;
  StreamInfo::StreamInfo* const stream_info_;
};

using FilterSharedPtr = Network::UdpSessionFilterSharedPtr;
using ReadFilterSharedPtr = Network::UdpSessionReadFilterSharedPtr;
using WriteFilterSharedPtr = Network::UdpSessionWriteFilterSharedPtr;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using WriteFilterCallbacks = Network::UdpSessionWriteFilterCallbacks;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using WriteFilterStatus = Network::UdpSessionWriteFilterStatus;
using FilterChainFactoryCallbacks = Network::UdpSessionFilterChainFactoryCallbacks;

/**
 * Per-session UDP Proxy Cluster configuration.
 */
class PerSessionCluster : public StreamInfo::FilterState::Object {
public:
  PerSessionCluster(absl::string_view cluster) : cluster_(cluster) {}
  const std::string& value() const { return cluster_; }
  absl::optional<std::string> serializeAsString() const override { return cluster_; }
  static const std::string& key();

private:
  const std::string cluster_;
};

class UdpProxyFilter : public Network::UdpListenerReadFilter,
                       public Upstream::ClusterUpdateCallbacks,
                       protected Logger::Loggable<Logger::Id::filter> {
public:
  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override {
    if (udp_proxy_stats_) {
      udp_proxy_stats_.value().addBytesReceived(data.buffer_->length());
    }

    return onDataInternal(data);
  }

  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

protected:
  class ActiveSession;
  class ClusterInfo;

  UdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                 const UdpProxyFilterConfigSharedPtr& config);
  ~UdpProxyFilter() override;

  virtual Network::FilterStatus onDataInternal(Network::UdpRecvData& data) PURE;

  ClusterInfo* getClusterInfo(const Network::UdpRecvData::LocalPeerAddresses& addresses);
  ClusterInfo* getClusterInfo(const std::string& cluster);
  ActiveSession* createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                               const Upstream::HostConstSharedPtr& optional_host,
                               bool defer_socket_creation);
  void removeSession(ActiveSession* session);

  struct ActiveReadFilter : public virtual ReadFilterCallbacks, LinkedObject<ActiveReadFilter> {
    ActiveReadFilter(ActiveSession& parent, ReadFilterSharedPtr filter)
        : parent_(parent), read_filter_(std::move(filter)) {}

    // SessionFilters::ReadFilterCallbacks
    uint64_t sessionId() const override { return parent_.sessionId(); };
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); };
    bool continueFilterChain() override { return parent_.onContinueFilterChain(this); }
    void injectDatagramToFilterChain(Network::UdpRecvData& data) override {
      parent_.onInjectReadDatagramToFilterChain(this, data);
    }

    ActiveSession& parent_;
    ReadFilterSharedPtr read_filter_;
    bool initialized_{false};
  };

  using ActiveReadFilterPtr = std::unique_ptr<ActiveReadFilter>;

  struct ActiveWriteFilter : public virtual WriteFilterCallbacks, LinkedObject<ActiveWriteFilter> {
    ActiveWriteFilter(ActiveSession& parent, WriteFilterSharedPtr filter)
        : parent_(parent), write_filter_(std::move(filter)) {}

    // SessionFilters::WriteFilterCallbacks
    uint64_t sessionId() const override { return parent_.sessionId(); };
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); };
    void injectDatagramToFilterChain(Network::UdpRecvData& data) override {
      parent_.onInjectWriteDatagramToFilterChain(this, data);
    }

    ActiveSession& parent_;
    WriteFilterSharedPtr write_filter_;
  };

  using ActiveWriteFilterPtr = std::unique_ptr<ActiveWriteFilter>;

  /**
   * An active session is similar to a TCP connection. It binds a 4-tuple (downstream IP/port, local
   * IP/port) to a selected upstream host for the purpose of packet forwarding. Unlike a TCP
   * connection, there is obviously no concept of session destruction beyond internally tracked data
   * such as an idle timeout, maximum packets, etc. Once a session is created, downstream packets
   * will be hashed to the same session and will be forwarded to the same upstream, using the same
   * local ephemeral IP/port.
   */
  class ActiveSession : public FilterChainFactoryCallbacks {
  public:
    ActiveSession(UdpProxyFilter& filter, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                  const Upstream::HostConstSharedPtr& host);
    ~ActiveSession() override;

    const Network::UdpRecvData::LocalPeerAddresses& addresses() const { return addresses_; }
    ClusterInfo* cluster() const { return cluster_; }
    absl::optional<std::reference_wrapper<const Upstream::Host>> host() const {
      if (host_) {
        return *host_;
      }

      return absl::nullopt;
    }

    bool onNewSession();
    void onData(Network::UdpRecvData& data);
    void processUpstreamDatagram(Network::UdpRecvData& data);
    void writeDownstream(Network::UdpRecvData& data);
    void resetIdleTimer();

    virtual bool shouldCreateUpstream() PURE;
    virtual bool createUpstream() PURE;
    virtual void writeUpstream(Network::UdpRecvData& data) PURE;
    virtual void onIdleTimer() PURE;

    bool createFilterChain() {
      return filter_.config_->sessionFilterFactory().createFilterChain(*this);
    }

    uint64_t sessionId() const { return session_id_; };
    StreamInfo::StreamInfo& streamInfo() { return udp_session_info_; };
    bool onContinueFilterChain(ActiveReadFilter* filter);
    void onInjectReadDatagramToFilterChain(ActiveReadFilter* filter, Network::UdpRecvData& data);
    void onInjectWriteDatagramToFilterChain(ActiveWriteFilter* filter, Network::UdpRecvData& data);
    virtual void onSessionComplete();

    // SessionFilters::FilterChainFactoryCallbacks
    void addReadFilter(ReadFilterSharedPtr filter) override {
      ActiveReadFilterPtr wrapper = std::make_unique<ActiveReadFilter>(*this, filter);
      filter->initializeReadFilterCallbacks(*wrapper);
      LinkedList::moveIntoListBack(std::move(wrapper), read_filters_);
    };

    void addWriteFilter(WriteFilterSharedPtr filter) override {
      ActiveWriteFilterPtr wrapper = std::make_unique<ActiveWriteFilter>(*this, filter);
      filter->initializeWriteFilterCallbacks(*wrapper);
      LinkedList::moveIntoList(std::move(wrapper), write_filters_);
    };

    void addFilter(FilterSharedPtr filter) override {
      ActiveReadFilterPtr read_wrapper = std::make_unique<ActiveReadFilter>(*this, filter);
      filter->initializeReadFilterCallbacks(*read_wrapper);
      LinkedList::moveIntoListBack(std::move(read_wrapper), read_filters_);

      ActiveWriteFilterPtr write_wrapper = std::make_unique<ActiveWriteFilter>(*this, filter);
      filter->initializeWriteFilterCallbacks(*write_wrapper);
      LinkedList::moveIntoList(std::move(write_wrapper), write_filters_);
    };

  protected:
    void fillSessionStreamInfo();

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

    static std::atomic<uint64_t> next_global_session_id_;

    UdpProxyFilter& filter_;
    const Network::UdpRecvData::LocalPeerAddresses addresses_;
    Upstream::HostConstSharedPtr host_;
    ClusterInfo* cluster_{nullptr};
    uint64_t session_id_;
    // TODO(mattklein123): Consider replacing an idle timer for each session with a last used
    // time stamp and a periodic scan of all sessions to look for timeouts. This solution is simple,
    // though it might not perform well for high volume traffic. Note that this is how TCP proxy
    // idle timeouts work so we should consider unifying the implementation if we move to a time
    // stamp and scan approach.
    const Event::TimerPtr idle_timer_;
    Event::TimerPtr access_log_flush_timer_;

    UdpProxySessionStats session_stats_{};
    StreamInfo::StreamInfoImpl udp_session_info_;
    std::list<ActiveReadFilterPtr> read_filters_;
    std::list<ActiveWriteFilterPtr> write_filters_;

  private:
    std::shared_ptr<Network::ConnectionInfoSetterImpl> createDownstreamConnectionInfoProvider();
    void onAccessLogFlushInterval();
    void rearmAccessLogFlushTimer();
    void disableAccessLogFlushTimer();
    bool setClusterInfo();

    bool cluster_connections_inc_{false};
    bool on_session_complete_called_{false};
  };

  using ActiveSessionSharedPtr = std::shared_ptr<ActiveSession>;

  class UdpActiveSession : public Network::UdpPacketProcessor, public ActiveSession {
  public:
    UdpActiveSession(UdpProxyFilter& filter, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                     const Upstream::HostConstSharedPtr& host);
    ~UdpActiveSession() override = default;

    // ActiveSession
    bool shouldCreateUpstream() override;
    bool createUpstream() override;
    void writeUpstream(Network::UdpRecvData& data) override;
    void onIdleTimer() override;

    // Network::UdpPacketProcessor
    void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                       Network::Address::InstanceConstSharedPtr peer_address,
                       Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                       Buffer::OwnedImpl saved_csmg) override;

    uint64_t maxDatagramSize() const override {
      return filter_.config_->upstreamSocketConfig().max_rx_datagram_size_;
    }

    void onDatagramsDropped(uint32_t dropped) override {
      if (cluster_) {
        cluster_->cluster_stats_.sess_rx_datagrams_dropped_.add(dropped);
      }
    }

    size_t numPacketsExpectedPerEventLoop() const final {
      // TODO(mattklein123) change this to a reasonable number if needed.
      return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
    }

    const Network::IoHandle::UdpSaveCmsgConfig& saveCmsgConfig() const override {
      static const Network::IoHandle::UdpSaveCmsgConfig empty_config{};
      return empty_config;
    };

  private:
    void onReadReady();
    void createUdpSocket(const Upstream::HostConstSharedPtr& host);

    // The socket is used for writing packets to the selected upstream host as well as receiving
    // packets from the upstream host. Note that a a local ephemeral port is bound on the first
    // write to the upstream host.
    Network::SocketPtr udp_socket_;
    // The socket has been connected to avoid port exhaustion.
    bool connected_{};
    const bool use_original_src_ip_;
  };

  /**
   * This type of active session is used when tunneling is enabled by configuration.
   * In this type of session, the upstream is HTTP stream, either a connect-udp request,
   * or a POST request.
   */
  class TunnelingActiveSession : public ActiveSession,
                                 public UpstreamTunnelCallbacks,
                                 public HttpStreamCallbacks,
                                 public std::enable_shared_from_this<TunnelingActiveSession> {
  public:
    TunnelingActiveSession(UdpProxyFilter& filter,
                           Network::UdpRecvData::LocalPeerAddresses&& addresses);
    ~TunnelingActiveSession() override = default;

    // ActiveSession
    bool shouldCreateUpstream() override;
    bool createUpstream() override;
    void writeUpstream(Network::UdpRecvData& data) override;
    void onIdleTimer() override;
    void onSessionComplete() override;

    // UpstreamTunnelCallbacks
    void onUpstreamEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;
    void onUpstreamData(Buffer::Instance& data, bool end_stream) override;

    // HttpStreamCallbacks
    void onStreamReady(StreamInfo::StreamInfo*, std::unique_ptr<HttpUpstream>&&,
                       const Upstream::HostDescription&, const Network::ConnectionInfoProvider&,
                       Ssl::ConnectionInfoConstSharedPtr) override;

    void onStreamFailure(ConnectionPool::PoolFailureReason, absl::string_view,
                         const Upstream::HostDescription&) override;

    void resetIdleTimer() override { ActiveSession::resetIdleTimer(); }

  private:
    using BufferedDatagramPtr = std::unique_ptr<Network::UdpRecvData>;

    bool establishUpstreamConnection();
    bool createConnectionPool();
    void maybeBufferDatagram(Network::UdpRecvData& data);
    void flushBuffer();
    void onRetryTimer();
    void resetRetryTimer();
    void disableRetryTimer();

    Event::TimerPtr retry_timer_;
    TunnelingConnectionPoolFactoryPtr conn_pool_factory_;
    std::unique_ptr<UdpLoadBalancerContext> load_balancer_context_;
    TunnelingConnectionPoolPtr conn_pool_;
    std::unique_ptr<HttpUpstream> upstream_;
    uint32_t connect_attempts_{};
    bool connecting_{};
    bool can_send_upstream_{};
    uint64_t buffered_bytes_{};
    std::queue<BufferedDatagramPtr> datagrams_buffer_;
  };

  struct LocalPeerHostAddresses {
    const Network::UdpRecvData::LocalPeerAddresses& local_peer_addresses_;
    absl::optional<std::reference_wrapper<const Upstream::Host>> host_;
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
        hash = absl::HashOf(hash, value.host_.value().get().address()->asStringView());
      }
      return hash;
    }
    size_t operator()(const ActiveSession* value) const {
      LocalPeerHostAddresses key{value->addresses(), value->host()};
      return this->operator()(key);
    }
    size_t operator()(const ActiveSessionSharedPtr& value) const {
      return this->operator()(value.get());
    }

  private:
    const bool consider_host_;
  };

  struct HeterogeneousActiveSessionEqual {
    // See description for HeterogeneousActiveSessionHash::is_transparent.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    HeterogeneousActiveSessionEqual(const bool consider_host) : consider_host_(consider_host) {}

    bool operator()(const ActiveSessionSharedPtr& lhs,
                    const Network::UdpRecvData::LocalPeerAddresses& rhs) const {
      return lhs->addresses() == rhs;
    }
    bool operator()(const ActiveSessionSharedPtr& lhs, const LocalPeerHostAddresses& rhs) const {
      return this->operator()(lhs, rhs.local_peer_addresses_) &&
             (consider_host_ ? &lhs->host().value().get() == &rhs.host_.value().get() : true);
    }
    bool operator()(const ActiveSessionSharedPtr& lhs, const ActiveSession* rhs) const {
      LocalPeerHostAddresses key{rhs->addresses(), rhs->host()};
      return this->operator()(lhs, key);
    }
    bool operator()(const ActiveSessionSharedPtr& lhs, const ActiveSessionSharedPtr& rhs) const {
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
  public:
    ClusterInfo(UdpProxyFilter& filter, Upstream::ThreadLocalCluster& cluster,
                absl::flat_hash_set<ActiveSession*>&& sessions);
    virtual ~ClusterInfo();
    void removeSessionHosts(ActiveSession* session);
    void addSession(const Upstream::Host* host, ActiveSession* session) {
      host_to_sessions_[host].emplace(session);
    }

    Upstream::HostSelectionResponse
    chooseHost(const Network::Address::InstanceConstSharedPtr& peer_address,
               StreamInfo::StreamInfo* stream_info) const;

    UdpProxyFilter& filter_;
    Upstream::ThreadLocalCluster& cluster_;
    Upstream::ClusterInfoConstSharedPtr cluster_info_;
    UdpProxyUpstreamStats cluster_stats_;
    absl::flat_hash_set<ActiveSession*> sessions_;

  private:
    static UdpProxyUpstreamStats generateStats(Stats::Scope& scope) {
      const auto final_prefix = "udp";
      return {ALL_UDP_PROXY_UPSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
    }

    Envoy::Common::CallbackHandlePtr member_update_cb_handle_;
    absl::flat_hash_map<const Upstream::Host*, absl::flat_hash_set<ActiveSession*>>
        host_to_sessions_;
  };

  using ClusterInfoPtr = std::unique_ptr<ClusterInfo>;
  using SessionStorageType =
      absl::flat_hash_set<ActiveSessionSharedPtr, HeterogeneousActiveSessionHash,
                          HeterogeneousActiveSessionEqual>;

  const UdpProxyFilterConfigSharedPtr config_;
  SessionStorageType sessions_;

private:
  ActiveSession* createSessionWithOptionalHost(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                               const Upstream::HostConstSharedPtr& host);

  virtual Network::SocketPtr createUdpSocket(const Upstream::HostConstSharedPtr& host) {
    // Virtual so this can be overridden in unit tests.
    return std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, host->address(),
                                                 nullptr, Network::SocketCreationOptions{});
  }

  void fillProxyStreamInfo();
  bool addOrUpdateCluster(const std::string& cluster_name);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) final;
  void onClusterRemoval(const std::string& cluster_name) override;

  const Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_;
  // Map for looking up cluster info with its name.
  absl::flat_hash_map<std::string, ClusterInfoPtr> cluster_infos_;

  absl::optional<StreamInfo::StreamInfoImpl> udp_proxy_stats_;
};

/**
 * Performs forwarding and replying data to one upstream host, selected when the first datagram
 * for a session is received. If the upstream host becomes unhealthy, a new one is selected.
 */
class StickySessionUdpProxyFilter : public virtual UdpProxyFilter {
public:
  StickySessionUdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                              const UdpProxyFilterConfigSharedPtr& config)
      : UdpProxyFilter(callbacks, config) {}

  // UdpProxyFilter
  Network::FilterStatus onDataInternal(Network::UdpRecvData& data) override;
};

/**
 * On each data chunk selects another host using underlying load balancing method and communicates
 * with that host.
 */
class PerPacketLoadBalancingUdpProxyFilter : public virtual UdpProxyFilter {
public:
  PerPacketLoadBalancingUdpProxyFilter(Network::UdpReadFilterCallbacks& callbacks,
                                       const UdpProxyFilterConfigSharedPtr& config)
      : UdpProxyFilter(callbacks, config) {}

  // UdpProxyFilter
  Network::FilterStatus onDataInternal(Network::UdpRecvData& data) override;
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

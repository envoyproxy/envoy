#pragma once

#include <queue>

#include "envoy/access_log/access_log.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/http/header_evaluator.h"
#include "envoy/network/filter.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/stream_info/uint32_accessor.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/header_parser.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
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

/**
 * Configuration for raw UDP tunneling over HTTP streams.
 */
class UdpTunnelingConfig {
public:
  virtual ~UdpTunnelingConfig() = default;

  virtual const std::string proxyHost(const StreamInfo::StreamInfo& stream_info) const PURE;
  virtual const std::string targetHost(const StreamInfo::StreamInfo& stream_info) const PURE;
  virtual const absl::optional<uint32_t>& proxyPort() const PURE;
  virtual uint32_t defaultTargetPort() const PURE;
  virtual bool usePost() const PURE;
  virtual const std::string& postPath() const PURE;
  virtual Http::HeaderEvaluator& headerEvaluator() const PURE;
  virtual uint32_t maxConnectAttempts() const PURE;
  virtual bool bufferEnabled() const PURE;
  virtual uint32_t maxBufferedDatagrams() const PURE;
  virtual uint64_t maxBufferedBytes() const PURE;

  virtual void
  propagateResponseHeaders(Http::ResponseHeaderMapPtr&& headers,
                           const StreamInfo::FilterStateSharedPtr& filter_state) const PURE;

  virtual void
  propagateResponseTrailers(Http::ResponseTrailerMapPtr&& trailers,
                            const StreamInfo::FilterStateSharedPtr& filter_state) const PURE;
};

using UdpTunnelingConfigPtr = std::unique_ptr<const UdpTunnelingConfig>;

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
  virtual const std::vector<AccessLog::InstanceSharedPtr>& sessionAccessLogs() const PURE;
  virtual const std::vector<AccessLog::InstanceSharedPtr>& proxyAccessLogs() const PURE;
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
                         const StreamInfo::StreamInfo* stream_info)
      : stream_info_(stream_info) {
    if (hash_policy) {
      hash_ = hash_policy->generateHash(*peer_address);
    }
  }

  absl::optional<uint64_t> computeHashKey() override { return hash_; }
  const StreamInfo::StreamInfo* requestStreamInfo() const override { return stream_info_; }

private:
  absl::optional<uint64_t> hash_;
  const StreamInfo::StreamInfo* stream_info_;
};

/**
 * Represents an upstream HTTP stream handler, which is able to send data and signal
 * the downstream about upstream events.
 */
class HttpUpstream {
public:
  virtual ~HttpUpstream() = default;

  /**
   * Used to encode data upstream using the HTTP stream.
   * @param data supplies the data to encode upstream.
   */
  virtual void encodeData(Buffer::Instance& data) PURE;

  /**
   * Called when an event is received on the downstream session.
   * @param event supplies the event which occurred.
   */
  virtual void onDownstreamEvent(Network::ConnectionEvent event) PURE;
};

/**
 * Callbacks to signal the status of upstream HTTP stream creation to the downstream.
 */
class HttpStreamCallbacks {
public:
  virtual ~HttpStreamCallbacks() = default;

  /**
   * Called when the stream is ready, after receiving successful header response from
   * the upstream.
   * @param info supplies the stream info object associated with the upstream connection.
   * @param upstream supplies the HTTP upstream for the tunneling stream.
   * @param host supplies the description of the upstream host that will carry the request.
   * @param address_provider supplies the address provider of the upstream connection.
   * @param ssl_info supplies the ssl information of the upstream connection.
   */
  virtual void onStreamReady(StreamInfo::StreamInfo* info, std::unique_ptr<HttpUpstream>&& upstream,
                             Upstream::HostDescriptionConstSharedPtr& host,
                             const Network::ConnectionInfoProvider& address_provider,
                             Ssl::ConnectionInfoConstSharedPtr ssl_info) PURE;

  /**
   * Called to indicate a failure for when establishing the HTTP stream.
   * @param reason supplies the failure reason.
   * @param failure_reason failure reason string.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onStreamFailure(ConnectionPool::PoolFailureReason reason,
                               absl::string_view failure_reason,
                               Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Called to reset the idle timer.
   */
  virtual void resetIdleTimer() PURE;
};

/**
 * Callbacks to signal the status of upstream HTTP stream creation to the connection pool.
 */
class TunnelCreationCallbacks {
public:
  virtual ~TunnelCreationCallbacks() = default;

  /**
   * Called when success response headers returned from the upstream.
   * @param request_encoder the upstream request encoder associated with the stream.
   */
  virtual void onStreamSuccess(Http::RequestEncoder& request_encoder) PURE;

  /**
   * Called when failed to create the upstream HTTP stream.
   */
  virtual void onStreamFailure() PURE;
};

/**
 * Callbacks that allows upstream stream to signal events to the downstream.
 */
class UpstreamTunnelCallbacks {
public:
  virtual ~UpstreamTunnelCallbacks() = default;

  /**
   * Called when an event was raised by the upstream.
   * @param event the event.
   */
  virtual void onUpstreamEvent(Network::ConnectionEvent event) PURE;

  /**
   * Called when the upstream buffer went above high watermark.
   */
  virtual void onAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the upstream buffer went below high watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() PURE;

  /**
   * Called when an event was raised by the upstream.
   * @param data the data received from the upstream.
   * @param end_stream indicates if the received data is ending the stream.
   */
  virtual void onUpstreamData(Buffer::Instance& data, bool end_stream) PURE;
};

class HttpUpstreamImpl : public HttpUpstream, protected Http::StreamCallbacks {
public:
  HttpUpstreamImpl(UpstreamTunnelCallbacks& upstream_callbacks, const UdpTunnelingConfig& config,
                   StreamInfo::StreamInfo& downstream_info)
      : response_decoder_(*this), upstream_callbacks_(upstream_callbacks),
        downstream_info_(downstream_info), tunnel_config_(config) {}
  ~HttpUpstreamImpl() override;

  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }
  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl);
  void setTunnelCreationCallbacks(TunnelCreationCallbacks& callbacks) {
    tunnel_creation_callbacks_.emplace(callbacks);
  }

  // HttpUpstream
  void encodeData(Buffer::Instance& data) override;
  void onDownstreamEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      resetEncoder(event, /*by_downstream=*/true);
    }
  };

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason, absl::string_view) override {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }

  void onAboveWriteBufferHighWatermark() override {
    upstream_callbacks_.onAboveWriteBufferHighWatermark();
  }

  void onBelowWriteBufferLowWatermark() override {
    upstream_callbacks_.onBelowWriteBufferLowWatermark();
  }

private:
  class ResponseDecoder : public Http::ResponseDecoder {
  public:
    ResponseDecoder(HttpUpstreamImpl& parent) : parent_(parent) {}

    // Http::ResponseDecoder
    void decodeMetadata(Http::MetadataMapPtr&&) override {}
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(ResponseDecoder);
    }

    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      bool is_valid_response;
      if (parent_.tunnel_config_.usePost()) {
        auto status = Http::Utility::getResponseStatus(*headers);
        is_valid_response = Http::CodeUtility::is2xx(status);
      } else {
        is_valid_response = Http::HeaderUtility::isConnectUdpResponse(*headers);
      }

      parent_.tunnel_config_.propagateResponseHeaders(std::move(headers),
                                                      parent_.downstream_info_.filterState());

      if (!is_valid_response || end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
      } else if (parent_.tunnel_creation_callbacks_.has_value()) {
        parent_.tunnel_creation_callbacks_.value().get().onStreamSuccess(*parent_.request_encoder_);
        parent_.tunnel_creation_callbacks_.reset();
      }
    }

    void decodeData(Buffer::Instance& data, bool end_stream) override {
      parent_.upstream_callbacks_.onUpstreamData(data, end_stream);
      if (end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
      }
    }

    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override {
      parent_.tunnel_config_.propagateResponseTrailers(std::move(trailers),
                                                       parent_.downstream_info_.filterState());
      parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
    }

  private:
    HttpUpstreamImpl& parent_;
  };

  const std::string resolveTargetTunnelPath();
  void resetEncoder(Network::ConnectionEvent event, bool by_downstream = false);

  ResponseDecoder response_decoder_;
  Http::RequestEncoder* request_encoder_{};
  UpstreamTunnelCallbacks& upstream_callbacks_;
  StreamInfo::StreamInfo& downstream_info_;
  const UdpTunnelingConfig& tunnel_config_;
  absl::optional<std::reference_wrapper<TunnelCreationCallbacks>> tunnel_creation_callbacks_;
};

/**
 * Provide method to create upstream HTTP stream for UDP tunneling.
 */
class TunnelingConnectionPool {
public:
  virtual ~TunnelingConnectionPool() = default;

  /**
   * Called to create a TCP connection and HTTP stream.
   *
   * @param callbacks callbacks to communicate stream failure or creation on.
   */
  virtual void newStream(HttpStreamCallbacks& callbacks) PURE;

  /**
   * Called when an event is received on the downstream session.
   * @param event supplies the event which occurred.
   */
  virtual void onDownstreamEvent(Network::ConnectionEvent event) PURE;
};

using TunnelingConnectionPoolPtr = std::unique_ptr<TunnelingConnectionPool>;

class TunnelingConnectionPoolImpl : public TunnelingConnectionPool,
                                    public TunnelCreationCallbacks,
                                    public Http::ConnectionPool::Callbacks,
                                    public Logger::Loggable<Logger::Id::upstream> {
public:
  TunnelingConnectionPoolImpl(Upstream::ThreadLocalCluster& thread_local_cluster,
                              Upstream::LoadBalancerContext* context,
                              const UdpTunnelingConfig& tunnel_config,
                              UpstreamTunnelCallbacks& upstream_callbacks,
                              StreamInfo::StreamInfo& downstream_info);
  ~TunnelingConnectionPoolImpl() override = default;

  bool valid() const { return conn_pool_data_.has_value(); }

  void onDownstreamEvent(Network::ConnectionEvent event) override {
    if (upstream_) {
      upstream_->onDownstreamEvent(event);
    }
  }

  // TunnelingConnectionPool
  void newStream(HttpStreamCallbacks& callbacks) override;

  // TunnelCreationCallbacks
  void onStreamSuccess(Http::RequestEncoder& request_encoder) override {
    callbacks_->onStreamReady(upstream_info_, std::move(upstream_), upstream_host_,
                              request_encoder.getStream().connectionInfoProvider(), ssl_info_);
  }

  void onStreamFailure() override {
    callbacks_->onStreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "",
                                upstream_host_);
  }

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                     absl::string_view failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& request_encoder,
                   Upstream::HostDescriptionConstSharedPtr upstream_host,
                   StreamInfo::StreamInfo& upstream_info, absl::optional<Http::Protocol>) override;

private:
  absl::optional<Upstream::HttpPoolData> conn_pool_data_{};
  HttpStreamCallbacks* callbacks_{};
  UpstreamTunnelCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpUpstreamImpl> upstream_;
  Http::ConnectionPool::Cancellable* upstream_handle_{};
  const UdpTunnelingConfig& tunnel_config_;
  StreamInfo::StreamInfo& downstream_info_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  Ssl::ConnectionInfoConstSharedPtr ssl_info_;
  StreamInfo::StreamInfo* upstream_info_;
};

class TunnelingConnectionPoolFactory {
public:
  /**
   * Called to create a connection pool that can be used to create an upstream connection.
   *
   * @param thread_local_cluster the thread local cluster to use for conn pool creation.
   * @param context the load balancing context for this connection.
   * @param tunnel_config the tunneling config.
   * @param upstream_callbacks the callbacks to provide to the connection if successfully created.
   * @param stream_info is the downstream session stream info.
   * @return may be null if pool creation failed.
   */
  TunnelingConnectionPoolPtr createConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                                            Upstream::LoadBalancerContext* context,
                                            const UdpTunnelingConfig& tunnel_config,
                                            UpstreamTunnelCallbacks& upstream_callbacks,
                                            StreamInfo::StreamInfo& stream_info) const;
};

using TunnelingConnectionPoolFactoryPtr = std::unique_ptr<TunnelingConnectionPoolFactory>;

using FilterSharedPtr = Network::UdpSessionFilterSharedPtr;
using ReadFilterSharedPtr = Network::UdpSessionReadFilterSharedPtr;
using WriteFilterSharedPtr = Network::UdpSessionWriteFilterSharedPtr;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using WriteFilterCallbacks = Network::UdpSessionWriteFilterCallbacks;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using WriteFilterStatus = Network::UdpSessionWriteFilterStatus;
using FilterChainFactoryCallbacks = Network::UdpSessionFilterChainFactoryCallbacks;

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
  class ActiveSession;
  class ClusterInfo;

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
    ActiveSession(ClusterInfo& parent, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                  const Upstream::HostConstSharedPtr& host);
    ~ActiveSession() override;

    const Network::UdpRecvData::LocalPeerAddresses& addresses() const { return addresses_; }
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

    virtual bool createUpstream() PURE;
    virtual void writeUpstream(Network::UdpRecvData& data) PURE;
    virtual void onIdleTimer() PURE;

    bool createFilterChain() {
      return cluster_.filter_.config_->sessionFilterFactory().createFilterChain(*this);
    }

    uint64_t sessionId() const { return session_id_; };
    StreamInfo::StreamInfo& streamInfo() { return udp_session_info_; };
    bool onContinueFilterChain(ActiveReadFilter* filter);
    void onInjectReadDatagramToFilterChain(ActiveReadFilter* filter, Network::UdpRecvData& data);
    void onInjectWriteDatagramToFilterChain(ActiveWriteFilter* filter, Network::UdpRecvData& data);
    void onSessionComplete();

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

    ClusterInfo& cluster_;
    const Network::UdpRecvData::LocalPeerAddresses addresses_;
    Upstream::HostConstSharedPtr host_;
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

    bool on_session_complete_called_{false};
  };

  using ActiveSessionPtr = std::unique_ptr<ActiveSession>;

  class UdpActiveSession : public Network::UdpPacketProcessor, public ActiveSession {
  public:
    UdpActiveSession(ClusterInfo& parent, Network::UdpRecvData::LocalPeerAddresses&& addresses,
                     const Upstream::HostConstSharedPtr& host);
    ~UdpActiveSession() override = default;

    // ActiveSession
    bool createUpstream() override;
    void writeUpstream(Network::UdpRecvData& data) override;
    void onIdleTimer() override;

    // Network::UdpPacketProcessor
    void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                       Network::Address::InstanceConstSharedPtr peer_address,
                       Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                       Buffer::RawSlice saved_csmg) override;

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
                                 public HttpStreamCallbacks {
  public:
    TunnelingActiveSession(ClusterInfo& parent,
                           Network::UdpRecvData::LocalPeerAddresses&& addresses);
    ~TunnelingActiveSession() override = default;

    // ActiveSession
    bool createUpstream() override;
    void writeUpstream(Network::UdpRecvData& data) override;
    void onIdleTimer() override;

    // UpstreamTunnelCallbacks
    void onUpstreamEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;
    void onUpstreamData(Buffer::Instance& data, bool end_stream) override;

    // HttpStreamCallbacks
    void onStreamReady(StreamInfo::StreamInfo*, std::unique_ptr<HttpUpstream>&&,
                       Upstream::HostDescriptionConstSharedPtr&,
                       const Network::ConnectionInfoProvider&,
                       Ssl::ConnectionInfoConstSharedPtr) override;

    void onStreamFailure(ConnectionPool::PoolFailureReason, absl::string_view,
                         Upstream::HostDescriptionConstSharedPtr) override;

    void resetIdleTimer() override { ActiveSession::resetIdleTimer(); }

  private:
    using BufferedDatagramPtr = std::unique_ptr<Network::UdpRecvData>;

    bool establishUpstreamConnection();
    bool createConnectionPool();
    void maybeBufferDatagram(Network::UdpRecvData& data);
    void flushBuffer();

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
             (consider_host_ ? &lhs->host().value().get() == &rhs.host_.value().get() : true);
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
    void removeSession(ActiveSession* session);
    void addSession(const Upstream::Host* host, ActiveSession* session) {
      host_to_sessions_[host].emplace(session);
    }

    Upstream::HostConstSharedPtr
    chooseHost(const Network::Address::InstanceConstSharedPtr& peer_address,
               const StreamInfo::StreamInfo* stream_info) const;

    UdpProxyFilter& filter_;
    Upstream::ThreadLocalCluster& cluster_;
    UdpProxyUpstreamStats cluster_stats_;

  protected:
    ActiveSession* createSession(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                 const Upstream::HostConstSharedPtr& optional_host,
                                 bool defer_socket_creation);

    SessionStorageType sessions_;

  private:
    static UdpProxyUpstreamStats generateStats(Stats::Scope& scope) {
      const auto final_prefix = "udp";
      return {ALL_UDP_PROXY_UPSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
    }

    ActiveSession*
    createSessionWithOptionalHost(Network::UdpRecvData::LocalPeerAddresses&& addresses,
                                  const Upstream::HostConstSharedPtr& host);

    Envoy::Common::CallbackHandlePtr member_update_cb_handle_;
    absl::flat_hash_map<const Upstream::Host*, absl::flat_hash_set<ActiveSession*>>
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

  virtual Network::SocketPtr createUdpSocket(const Upstream::HostConstSharedPtr& host) {
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

#pragma once

#include "envoy/http/header_evaluator.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/response_decoder_impl_base.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

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
  virtual const BackOffStrategyPtr& backoffStrategy() const PURE;
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
                             const Upstream::HostDescription& host,
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
                               const Upstream::HostDescription& host) PURE;

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
      resetEncoder(event, /*by_local_close=*/true);
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
  class ResponseDecoder : public Http::ResponseDecoderImplBase {
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

  /**
   * Resets the encoder for the upstream connection.
   * @param event the event that caused the reset.
   * @param by_local_close whether the reset was initiated by a local close (e.g. session idle
   * timeout, envoy termination, etc.) or by upstream close.
   */
  void resetEncoder(Network::ConnectionEvent event, bool by_local_close = false);

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
    callbacks_->onStreamReady(upstream_info_, std::move(upstream_), *upstream_host_,
                              request_encoder.getStream().connectionInfoProvider(), ssl_info_);
  }

  void onStreamFailure() override {
    callbacks_->onStreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "",
                                *upstream_host_);
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

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

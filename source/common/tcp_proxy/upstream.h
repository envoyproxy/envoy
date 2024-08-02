#pragma once

#include <memory>

#include "envoy/http/conn_pool.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tcp/upstream.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/hash_policy.h"
#include "source/common/http/null_route_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/router.h"
#include "source/extensions/early_data/default_early_data_policy.h"

namespace Envoy {
namespace TcpProxy {

constexpr absl::string_view DisableTunnelingFilterStateKey = "envoy.tcp_proxy.disable_tunneling";

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::LoadBalancerContext* context,
              Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
              StreamInfo::StreamInfo& downstream_info);
  ~TcpConnPool() override;

  bool valid() const { return conn_pool_data_.has_value(); }

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  absl::optional<Upstream::TcpPoolData> conn_pool_data_{};
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  StreamInfo::StreamInfo& downstream_info_;
};

class HttpUpstream;
class CombinedUpstream;

// This class is specific to TCP proxy connection pool and enables TCP proxying mode
// for HTTP upstreams. This is currently only needed for HTTP/1 client codec that half closes
// upstream network connection after encoding end_stream in TCP proxy (i.e. via CONNECT).
class RouterUpstreamRequest : public Router::UpstreamRequest {
public:
  using Router::UpstreamRequest::UpstreamRequest;

  void onPoolReady(std::unique_ptr<Router::GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::ConnectionInfoProvider& address_provider,
                   StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) override {
    upstream->enableTcpTunneling();
    Router::UpstreamRequest::onPoolReady(std::move(upstream), host, address_provider, info,
                                         protocol);
  }
};

class HttpConnPool : public GenericConnPool, public Http::ConnectionPool::Callbacks {
public:
  HttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
               Upstream::LoadBalancerContext* context, const TunnelingConfigHelper& config,
               Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
               Http::StreamDecoderFilterCallbacks&, Http::CodecType type,
               StreamInfo::StreamInfo& downstream_info);

  using RouterUpstreamRequestPtr = std::unique_ptr<RouterUpstreamRequest>;
  ~HttpConnPool() override;

  bool valid() const { return conn_pool_data_.has_value() || generic_conn_pool_; }
  Http::CodecType codecType() const { return type_; }
  std::unique_ptr<Router::GenericConnPool> createConnPool(Upstream::ThreadLocalCluster&,
                                                          Upstream::LoadBalancerContext* context,
                                                          absl::optional<Http::Protocol> protocol);

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Http::ConnectionPool::Callbacks,
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& request_encoder,
                   Upstream::HostDescriptionConstSharedPtr host, StreamInfo::StreamInfo& info,
                   absl::optional<Http::Protocol>) override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr, bool);
  void onHttpPoolReady(Upstream::HostDescriptionConstSharedPtr& host,
                       Ssl::ConnectionInfoConstSharedPtr ssl_info);

  class Callbacks {
  public:
    Callbacks(HttpConnPool& conn_pool, Upstream::HostDescriptionConstSharedPtr host,
              Ssl::ConnectionInfoConstSharedPtr ssl_info)
        : conn_pool_(&conn_pool), host_(host), ssl_info_(ssl_info) {}
    virtual ~Callbacks() = default;
    virtual void onSuccess(Http::RequestEncoder* request_encoder) {
      ASSERT(conn_pool_ != nullptr);
      if (!Runtime::runtimeFeatureEnabled(
              "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
        ASSERT(request_encoder != nullptr);
        conn_pool_->onGenericPoolReady(host_, request_encoder->getStream().connectionInfoProvider(),
                                       ssl_info_);
        return;
      }

      Network::ConnectionInfoProviderSharedPtr local_connection_info_provider(
          std::make_shared<Network::ConnectionInfoSetterImpl>(
              Network::Utility::getCanonicalIpv4LoopbackAddress(),
              Network::Utility::getCanonicalIpv4LoopbackAddress()));

      conn_pool_->onGenericPoolReady(host_, *local_connection_info_provider.get(), ssl_info_);
    }
    virtual void onFailure() {
      ASSERT(conn_pool_ != nullptr);
      conn_pool_->callbacks_->onGenericPoolFailure(
          ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "", host_);
    }

  protected:
    Callbacks() = default;

  private:
    HttpConnPool* conn_pool_{};
    Upstream::HostDescriptionConstSharedPtr host_;
    Ssl::ConnectionInfoConstSharedPtr ssl_info_;
  };

private:
  void onGenericPoolReady(Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info);
  const TunnelingConfigHelper& config_;
  Http::CodecType type_;
  absl::optional<Upstream::HttpPoolData> conn_pool_data_{};
  Http::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_filter_callbacks_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
  std::unique_ptr<CombinedUpstream> combined_upstream_;
  StreamInfo::StreamInfo& downstream_info_;
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool_;
};

class TcpUpstream : public GenericUpstream {
public:
  TcpUpstream(Tcp::ConnectionPool::ConnectionDataPtr&& data,
              Tcp::ConnectionPool::UpstreamCallbacks& callbacks);

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  bool startUpstreamSecureTransport() override;
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override;

private:
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

class HttpUpstream : public GenericUpstream, protected Http::StreamCallbacks {
public:
  using TunnelingConfig =
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

  HttpUpstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
               const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info,
               Http::CodecType type);
  ~HttpUpstream() override;
  bool isValidResponse(const Http::ResponseHeaderMap&);

  void doneReading();
  void doneWriting();
  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  // HTTP upstream must not implement converting upstream transport
  // socket from non-secure to secure mode.
  bool startUpstreamSecureTransport() override { return false; }

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  virtual void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl);
  void setConnPoolCallbacks(std::unique_ptr<HttpConnPool::Callbacks>&& callbacks) {
    conn_pool_callbacks_ = std::move(callbacks);
  }
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override { return nullptr; }

protected:
  void resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  // The encoder offered by the upstream http client.
  Http::RequestEncoder* request_encoder_{};
  // The config object that is owned by the downstream network filter chain factory.
  const TunnelingConfigHelper& config_;
  // The downstream info that is owned by the downstream connection.
  StreamInfo::StreamInfo& downstream_info_;
  std::unique_ptr<Http::RequestHeaderMapImpl> downstream_headers_;

private:
  Upstream::ClusterInfoConstSharedPtr cluster_;
  class DecoderShim : public Http::ResponseDecoder {
  public:
    DecoderShim(HttpUpstream& parent) : parent_(parent) {}
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      bool is_valid_response = parent_.isValidResponse(*headers);
      parent_.config_.propagateResponseHeaders(std::move(headers),
                                               parent_.downstream_info_.filterState());
      if (!is_valid_response || end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
      } else if (parent_.conn_pool_callbacks_ != nullptr) {
        parent_.conn_pool_callbacks_->onSuccess(parent_.request_encoder_);
        parent_.conn_pool_callbacks_.reset();
      }
    }
    void decodeData(Buffer::Instance& data, bool end_stream) override {
      parent_.upstream_callbacks_.onUpstreamData(data, end_stream);
      if (end_stream) {
        parent_.doneReading();
      }
    }
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override {
      parent_.config_.propagateResponseTrailers(std::move(trailers),
                                                parent_.downstream_info_.filterState());
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.tcp_tunneling_send_downstream_fin_on_upstream_trailers")) {
        Buffer::OwnedImpl data;
        parent_.upstream_callbacks_.onUpstreamData(data, /* end_stream = */ true);
      }

      parent_.doneReading();
    }
    void decodeMetadata(Http::MetadataMapPtr&&) override {}
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(DecoderShim);
    }

  private:
    HttpUpstream& parent_;
  };
  DecoderShim response_decoder_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  const Http::CodecType type_;
  bool read_half_closed_{};
  bool write_half_closed_{};

  // Used to defer onGenericPoolReady and onGenericPoolFailure to the reception
  // of the CONNECT response or the resetEncoder.
  std::unique_ptr<HttpConnPool::Callbacks> conn_pool_callbacks_;
};

class CombinedUpstream : public GenericUpstream, public Envoy::Router::RouterFilterInterface {
public:
  CombinedUpstream(HttpConnPool& http_conn_pool, Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                   Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                   const TunnelingConfigHelper& config, StreamInfo::StreamInfo& downstream_info);
  ~CombinedUpstream() override = default;
  using UpstreamRequest = Router::UpstreamRequest;
  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }
  void doneReading();
  void doneWriting();
  using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;
  void setRouterUpstreamRequest(UpstreamRequestPtr);
  void newStream(GenericConnectionPoolCallbacks& callbacks);
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;
  bool isValidResponse(const Http::ResponseHeaderMap&);
  bool readDisable(bool disable) override;
  void setConnPoolCallbacks(std::unique_ptr<HttpConnPool::Callbacks>&& callbacks) {
    conn_pool_callbacks_ = std::move(callbacks);
  }
  void addBytesSentCallback(Network::Connection::BytesSentCb) override{};
  // HTTP upstream must not implement converting upstream transport
  // socket from non-secure to secure mode.
  bool startUpstreamSecureTransport() override { return false; }
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override { return nullptr; }

  // Router::RouterFilterInterface
  void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                         UpstreamRequest& upstream_request, bool end_stream) override;
  void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                      bool end_stream) override;
  void onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&&, UpstreamRequest&) override {}
  void onUpstreamTrailers(Http::ResponseTrailerMapPtr&&, UpstreamRequest&) override;
  void onUpstreamMetadata(Http::MetadataMapPtr&&) override {}
  void onUpstreamReset(Http::StreamResetReason stream_reset_reason,
                       absl::string_view transport_failure_reason, UpstreamRequest&) override;
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                              bool pool_success) override {
    parent_.onUpstreamHostSelected(host, pool_success);
  }
  void onPerTryTimeout(UpstreamRequest&) override {}
  void onPerTryIdleTimeout(UpstreamRequest&) override {}
  void onStreamMaxDurationReached(UpstreamRequest&) override {}
  Http::StreamDecoderFilterCallbacks* callbacks() override { return &decoder_filter_callbacks_; }
  Upstream::ClusterInfoConstSharedPtr cluster() override {
    return decoder_filter_callbacks_.clusterInfo();
  }
  Router::FilterConfig& config() override {
    return const_cast<Router::FilterConfig&>(config_.routerFilterConfig());
  }
  Router::TimeoutData timeout() override { return {}; }
  absl::optional<std::chrono::milliseconds> dynamicMaxStreamDuration() const override {
    return absl::nullopt;
  }
  Http::RequestHeaderMap* downstreamHeaders() override;
  Http::RequestTrailerMap* downstreamTrailers() override { return nullptr; }
  bool downstreamResponseStarted() const override { return false; }
  bool downstreamEndStream() const override { return false; }
  uint32_t attemptCount() const override { return 0; }

protected:
  void onResetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  // The config object that is owned by the downstream network filter chain factory.
  const TunnelingConfigHelper& config_;
  // The downstream info that is owned by the downstream connection.
  StreamInfo::StreamInfo& downstream_info_;
  std::unique_ptr<Http::RequestHeaderMapImpl> downstream_headers_;
  HttpConnPool& parent_;

private:
  Http::StreamDecoderFilterCallbacks& decoder_filter_callbacks_;
  class DecoderShim : public Http::ResponseDecoder {
  public:
    DecoderShim(CombinedUpstream& parent) : parent_(parent) {}
    // Http::ResponseDecoder
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      bool is_valid_response = parent_.isValidResponse(*headers);
      parent_.config_.propagateResponseHeaders(std::move(headers),
                                               parent_.downstream_info_.filterState());
      if (!is_valid_response || end_stream) {
        parent_.onResetEncoder(Network::ConnectionEvent::LocalClose);
      } else if (parent_.conn_pool_callbacks_ != nullptr) {
        parent_.conn_pool_callbacks_->onSuccess(nullptr /*parent_.request_encoder_*/);
        parent_.conn_pool_callbacks_.reset();
      }
    }
    void decodeData(Buffer::Instance& data, bool end_stream) override {
      parent_.upstream_callbacks_.onUpstreamData(data, end_stream);
      if (end_stream) {
        parent_.doneReading();
      }
    }
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override {
      parent_.config_.propagateResponseTrailers(std::move(trailers),
                                                parent_.downstream_info_.filterState());
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.tcp_tunneling_send_downstream_fin_on_upstream_trailers")) {
        Buffer::OwnedImpl data;
        parent_.upstream_callbacks_.onUpstreamData(data, /* end_stream = */ true);
      }
      parent_.doneReading();
    }
    void decodeMetadata(Http::MetadataMapPtr&&) override {}
    void dumpState(std::ostream& os, int indent_level) const override {
      DUMP_STATE_UNIMPLEMENTED(DecoderShim);
    }

  private:
    CombinedUpstream& parent_;
  };
  DecoderShim response_decoder_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpConnPool::Callbacks> conn_pool_callbacks_;
  bool read_half_closed_{};
  bool write_half_closed_{};
  // upstream_request_ has to be destroyed first as they may use CombinedUpstream parent
  // during destruction.
  UpstreamRequestPtr upstream_request_;
};

} // namespace TcpProxy
} // namespace Envoy

#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tcp/upstream.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/router/header_parser.h"

namespace Envoy {
namespace TcpProxy {

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::LoadBalancerContext* context,
              Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks);
  ~TcpConnPool() override;

  bool valid() const { return conn_pool_ != nullptr; }

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Tcp::ConnectionPool::Instance* conn_pool_{};
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
};

class HttpUpstream;

class HttpConnPool : public GenericConnPool, public Http::ConnectionPool::Callbacks {
public:
  using TunnelingConfig =
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

  HttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
               Upstream::LoadBalancerContext* context, const TunnelingConfig& config,
               Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
               Http::CodecClient::Type type);
  ~HttpConnPool() override;

  // HTTP/3 upstreams are not supported at the moment.
  bool valid() const { return conn_pool_ != nullptr && type_ <= Http::CodecClient::Type::HTTP2; }

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks& callbacks) override;

  // Http::ConnectionPool::Callbacks,
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& request_encoder,
                   Upstream::HostDescriptionConstSharedPtr host, const StreamInfo::StreamInfo& info,
                   absl::optional<Http::Protocol>) override;

  class Callbacks {
  public:
    Callbacks(HttpConnPool& conn_pool, Upstream::HostDescriptionConstSharedPtr host,
              Ssl::ConnectionInfoConstSharedPtr ssl_info)
        : conn_pool_(&conn_pool), host_(host), ssl_info_(ssl_info) {}
    virtual ~Callbacks() = default;
    virtual void onSuccess(Http::RequestEncoder& request_encoder) {
      ASSERT(conn_pool_ != nullptr);
      conn_pool_->onGenericPoolReady(host_, request_encoder.getStream().connectionLocalAddress(),
                                     ssl_info_);
    }
    virtual void onFailure() {
      ASSERT(conn_pool_ != nullptr);
      conn_pool_->callbacks_->onGenericPoolFailure(
          ConnectionPool::PoolFailureReason::RemoteConnectionFailure, host_);
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
                          const Network::Address::InstanceConstSharedPtr& local_address,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info);
  const TunnelingConfig config_;
  Http::CodecClient::Type type_;
  Http::ConnectionPool::Instance* conn_pool_{};
  Http::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
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

private:
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

class HttpUpstream : public GenericUpstream, protected Http::StreamCallbacks {
public:
  using TunnelingConfig =
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

  ~HttpUpstream() override;
  virtual bool isValidResponse(const Http::ResponseHeaderMap&) PURE;

  void doneReading();
  void doneWriting();
  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  virtual void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) PURE;
  void setConnPoolCallbacks(std::unique_ptr<HttpConnPool::Callbacks>&& callbacks) {
    conn_pool_callbacks_ = std::move(callbacks);
  }

protected:
  HttpUpstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks, const TunnelingConfig& config);
  void resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  Http::RequestEncoder* request_encoder_{};
  const TunnelingConfig config_;
  std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;

private:
  class DecoderShim : public Http::ResponseDecoder {
  public:
    DecoderShim(HttpUpstream& parent) : parent_(parent) {}
    // Http::ResponseDecoder
    void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      if (!parent_.isValidResponse(*headers) || end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
      } else if (parent_.conn_pool_callbacks_ != nullptr) {
        parent_.conn_pool_callbacks_->onSuccess(*parent_.request_encoder_);
        parent_.conn_pool_callbacks_.reset();
      }
    }
    void decodeData(Buffer::Instance& data, bool end_stream) override {
      parent_.upstream_callbacks_.onUpstreamData(data, end_stream);
      if (end_stream) {
        parent_.doneReading();
      }
    }
    void decodeTrailers(Http::ResponseTrailerMapPtr&&) override {}
    void decodeMetadata(Http::MetadataMapPtr&&) override {}

  private:
    HttpUpstream& parent_;
  };
  DecoderShim response_decoder_;
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  bool read_half_closed_{};
  bool write_half_closed_{};

  // Used to defer onGenericPoolReady and onGenericPoolFailure to the reception
  // of the CONNECT response or the resetEncoder.
  std::unique_ptr<HttpConnPool::Callbacks> conn_pool_callbacks_;
};

class Http1Upstream : public HttpUpstream {
public:
  Http1Upstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks, const TunnelingConfig& config);

  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) override;
  bool isValidResponse(const Http::ResponseHeaderMap& headers) override;
};

class Http2Upstream : public HttpUpstream {
public:
  Http2Upstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks, const TunnelingConfig& config);

  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) override;
  bool isValidResponse(const Http::ResponseHeaderMap& headers) override;
};

} // namespace TcpProxy
} // namespace Envoy

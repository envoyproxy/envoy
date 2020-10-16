#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace TcpProxy {

class GenericConnectionPoolCallbacks;
class GenericUpstream;

// An API for wrapping either an HTTP or a TCP connection pool.
class GenericConnPool : public Logger::Loggable<Logger::Id::router> {
public:
  virtual ~GenericConnPool() = default;

  // Called to create a new HTTP stream or TCP connection. The implementation
  // is then responsible for calling either onPoolReady or onPoolFailure on the
  // supplied GenericConnectionPoolCallbacks.
  virtual void newStream(GenericConnectionPoolCallbacks* callbacks) PURE;
  // Returns true if there was a valid connection pool, false otherwise.
  virtual bool valid() const PURE;
};

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
              Upstream::LoadBalancerContext* context,
              Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks);
  ~TcpConnPool() override;

  // GenericConnPool
  bool valid() const override;
  void newStream(GenericConnectionPoolCallbacks* callbacks) override;

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
  HttpConnPool(const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
               Upstream::LoadBalancerContext* context, std::string hostname,
               Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks);
  ~HttpConnPool() override;

  // GenericConnPool
  bool valid() const override;
  void newStream(GenericConnectionPoolCallbacks* callbacks) override;

  // Http::ConnectionPool::Callbacks,
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& request_encoder,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo& info) override;

private:
  const std::string hostname_;
  Http::ConnectionPool::Instance* conn_pool_{};
  Http::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
};

// An API for the UpstreamRequest to get callbacks from either an HTTP or TCP
// connection pool.
class GenericConnectionPoolCallbacks {
public:
  virtual ~GenericConnectionPoolCallbacks() = default;

  virtual void onGenericPoolReady(StreamInfo::StreamInfo* info,
                                  std::unique_ptr<GenericUpstream>&& upstream,
                                  Upstream::HostDescriptionConstSharedPtr& host,
                                  const Network::Address::InstanceConstSharedPtr& local_address,
                                  Ssl::ConnectionInfoConstSharedPtr ssl_info) PURE;
  virtual void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    Upstream::HostDescriptionConstSharedPtr host) PURE;
};

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;
  // Calls readDisable on the upstream connection. Returns false if readDisable could not be
  // performed (e.g. if the connection is closed)
  virtual bool readDisable(bool disable) PURE;
  // Encodes data upstream.
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;
  // Adds a callback to be called when the data is sent to the kernel.
  virtual void addBytesSentCallback(Network::Connection::BytesSentCb cb) PURE;
  // Called when a Network::ConnectionEvent is received on the downstream connection, to allow the
  // upstream to do any cleanup.
  virtual Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) PURE;
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

class HttpUpstream : public GenericUpstream, Http::StreamCallbacks {
public:
  HttpUpstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks, const std::string& hostname);
  ~HttpUpstream() override;

  static bool isValidBytestreamResponse(const Http::ResponseHeaderMap& headers);

  void doneReading();
  void doneWriting();

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

  void setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl);

  Http::ResponseDecoder& responseDecoder() { return response_decoder_; }

private:
  void resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  class DecoderShim : public Http::ResponseDecoder {
  public:
    DecoderShim(HttpUpstream& parent) : parent_(parent) {}
    // Http::ResponseDecoder
    void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
      if (!isValidBytestreamResponse(*headers) || end_stream) {
        parent_.resetEncoder(Network::ConnectionEvent::LocalClose);
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

  Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  DecoderShim response_decoder_;
  Http::RequestEncoder* request_encoder_{};
  const std::string hostname_;
  bool read_half_closed_{};
  bool write_half_closed_{};
};

} // namespace TcpProxy
} // namespace Envoy

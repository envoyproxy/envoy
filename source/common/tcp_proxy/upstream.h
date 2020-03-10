#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace TcpProxy {

// Interface for a generic ConnectionHandle, which can wrap a TcpConnectionHandle
// or an HttpConnectionHandle
class ConnectionHandle {
public:
  virtual ~ConnectionHandle() {}
  // Cancel the conn pool request and close any excess pending requests.
  virtual void cancel() PURE;
};

// An implementation of ConnectionHandle which works with the Tcp::ConnectionPool.
class TcpConnectionHandle : public ConnectionHandle {
public:
  TcpConnectionHandle(Tcp::ConnectionPool::Cancellable* handle) : upstream_handle_(handle) {}

  void cancel() override {
    upstream_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess);
  }

private:
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
};

class HttpConnectionHandle : public ConnectionHandle {
public:
  HttpConnectionHandle(Http::ConnectionPool::Cancellable* handle) : upstream_http_handle_(handle) {}
  void cancel() override { upstream_http_handle_->cancel(); }

private:
  Http::ConnectionPool::Cancellable* upstream_http_handle_{};
};

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream {
public:
  virtual ~GenericUpstream() {}
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
  ~HttpUpstream();

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

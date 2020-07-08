#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

#include "common/tcp_proxy/upstream_interface.h"

namespace Envoy {
namespace TcpProxy {

class TcpUpstream : public Envoy::TcpProxy::GenericUpstream {
public:
  TcpUpstream(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& data,
              Envoy::Tcp::ConnectionPool::UpstreamCallbacks& callbacks);

  // TcpProxy::GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Envoy::Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) override;

private:
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

// An implementation of ConnectionHandle which works with the Tcp::ConnectionPool.
class TcpConnectionHandle : public Envoy::TcpProxy::ConnectionHandle,
                            public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnectionHandle(Envoy::Tcp::ConnectionPool::Cancellable* handle,
                      Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                      Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callbacks)
      : tcp_upstream_handle_(handle), tcp_upstream_callbacks_(upstream_callbacks),
        generic_pool_callbacks_(generic_pool_callbacks) {}

  ~TcpConnectionHandle() override {
    if (tcp_upstream_handle_ != nullptr) {
      tcp_upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::CloseExcess);
    }
  }

  void cancel() override {
    if (tcp_upstream_handle_ != nullptr) {
      tcp_upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::CloseExcess);
      tcp_upstream_handle_ = nullptr;
    }
  }

  void complete() override { tcp_upstream_handle_ = nullptr; }

  Envoy::TcpProxy::GenericUpstreamSharedPtr upstream() override { return tcp_upstream_; }

  bool failedOnPool() override { return tcp_upstream_handle_ == nullptr; }

  bool failedOnConnection() override { return has_failure_; }

  bool isConnecting() override {
    return tcp_upstream_handle_ != nullptr && tcp_upstream_ == nullptr;
  }

  // Envoy::Tcp::ConnectionPool::Callbacks
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

  void onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

  void setUpstreamHandle(Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle) {
    ASSERT(tcp_upstream_handle_ == nullptr);
    tcp_upstream_handle_ = upstream_handle;
  }

private:
  // The handle to cancel the callback to set the tcp upstream.
  Envoy::Tcp::ConnectionPool::Cancellable* tcp_upstream_handle_{};
  Envoy::Tcp::ConnectionPool::UpstreamCallbacks& tcp_upstream_callbacks_;
  Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callbacks_;
  std::shared_ptr<TcpUpstream> tcp_upstream_{};
  bool has_failure_{false};
};

class HttpUpstream : public Envoy::TcpProxy::GenericUpstream, Envoy::Http::StreamCallbacks {
public:
  HttpUpstream(Envoy::Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
               const std::string& hostname);
  ~HttpUpstream() override;

  static bool isValidBytestreamResponse(const Envoy::Http::ResponseHeaderMap& headers);

  void doneReading();
  void doneWriting();

  // TcpProxy::GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Envoy::Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) override;

  // Http::StreamCallbacks
  void onResetStream(Envoy::Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;
  void setRequestEncoder(Envoy::Http::RequestEncoder& request_encoder, bool is_ssl);
  Envoy::Http::ResponseDecoder& responseDecoder() { return response_decoder_; }

private:
  void resetEncoder(Network::ConnectionEvent event, bool inform_downstream = true);

  class ConnectionDataShim : public Envoy::Tcp::ConnectionPool::ConnectionData {
  public:
    ConnectionDataShim(const Network::Address::InstanceConstSharedPtr& local_address,
                       StreamInfo::StreamInfo& stream_info)
        : read_only_client_connection_(local_address, stream_info) {}
    class ReadOnlyClientConnection : public Network::ClientConnection {
    public:
      ReadOnlyClientConnection(const Network::Address::InstanceConstSharedPtr& local_address,
                               StreamInfo::StreamInfo& stream_info)
          : local_address_(local_address), stream_info_(stream_info) {}
      const Network::Address::InstanceConstSharedPtr& localAddress() const override {
        return local_address_;
      }
      const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
      StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
      uint64_t id() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void addWriteFilter(Network::WriteFilterSharedPtr) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void addFilter(Network::FilterSharedPtr) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void addReadFilter(Network::ReadFilterSharedPtr) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool initializeReadFilters() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void addConnectionCallbacks(Network::ConnectionCallbacks&) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void addBytesSentCallback(Network::Connection::BytesSentCb) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void enableHalfClose(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void close(Network::ConnectionCloseType) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      Event::Dispatcher& dispatcher() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      std::string nextProtocol() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void noDelay(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void readDisable(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void detectEarlyCloseWhenReadDisabled(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool readEnabled() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      const Network::Address::InstanceConstSharedPtr& remoteAddress() const override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      const Network::Address::InstanceConstSharedPtr& directRemoteAddress() const override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void setConnectionStats(const Network::Connection::ConnectionStats&) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      Ssl::ConnectionInfoConstSharedPtr ssl() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      absl::string_view requestedServerName() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      uint32_t bufferLimit() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      State state() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void write(Buffer::Instance&, bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void setBufferLimits(uint32_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool localAddressRestored() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool aboveHighWatermark() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void setDelayedCloseTimeout(std::chrono::milliseconds) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      absl::string_view transportFailureReason() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void connect() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

    private:
      const Network::Address::InstanceConstSharedPtr& local_address_;
      StreamInfo::StreamInfo& stream_info_;
    };

    Network::ClientConnection& connection() override { return read_only_client_connection_; }

    void setConnectionState(Envoy::Tcp::ConnectionPool::ConnectionStatePtr&&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addUpstreamCallbacks(Envoy::Tcp::ConnectionPool::UpstreamCallbacks&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    Envoy::Tcp::ConnectionPool::ConnectionState* connectionState() override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

  private:
    ReadOnlyClientConnection read_only_client_connection_;
  };

  class DecoderShim : public Envoy::Http::ResponseDecoder {
  public:
    DecoderShim(HttpUpstream& parent) : parent_(parent) {}
    // Http::ResponseDecoder
    void decode100ContinueHeaders(Envoy::Http::ResponseHeaderMapPtr&&) override {}
    void decodeHeaders(Envoy::Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
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
    void decodeTrailers(Envoy::Http::ResponseTrailerMapPtr&&) override {}
    void decodeMetadata(Envoy::Http::MetadataMapPtr&&) override {}

  private:
    HttpUpstream& parent_;
  };

  Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks_;
  DecoderShim response_decoder_;
  Envoy::Http::RequestEncoder* request_encoder_{};
  const std::string hostname_;
  bool read_half_closed_{};
  bool write_half_closed_{};
};

class HttpConnectionHandle : public Envoy::TcpProxy::ConnectionHandle,
                             public Envoy::Http::ConnectionPool::Callbacks {
public:
  HttpConnectionHandle(Envoy::Http::ConnectionPool::Cancellable* handle,
                       Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callbacks)
      : upstream_http_handle_(handle), generic_pool_callbacks_(generic_pool_callbacks) {}

  ~HttpConnectionHandle() override {
    if (upstream_http_handle_ != nullptr) {
      upstream_http_handle_->cancel(ConnectionPool::CancelPolicy::Default);
    }
  }

  // Envoy::TcpProxy::ConnectionHandle
  void cancel() override {
    if (upstream_http_handle_ != nullptr) {
      upstream_http_handle_->cancel(ConnectionPool::CancelPolicy::Default);
      upstream_http_handle_ = nullptr;
    }
  }

  void complete() override { upstream_http_handle_ = nullptr; }

  Envoy::TcpProxy::GenericUpstreamSharedPtr upstream() override { return http_upstream_; }

  bool failedOnPool() override {
    return http_upstream_ == nullptr && upstream_http_handle_ == nullptr;
  }

  bool failedOnConnection() override { return has_failure_; }

  bool isConnecting() override { return upstream_http_handle_ != nullptr; }

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(Envoy::ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

  void onPoolReady(Envoy::Http::RequestEncoder& request_encoder,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host,
                   StreamInfo::StreamInfo& info) override;

  void setUpstreamHandle(Envoy::Http::ConnectionPool::Cancellable* upstream_handle) {
    ASSERT(upstream_http_handle_ == nullptr);
    upstream_http_handle_ = upstream_handle;
  }

  void setUpstream(const std::shared_ptr<HttpUpstream>& http_upstream) {
    ASSERT(http_upstream_ == nullptr);
    http_upstream_ = http_upstream;
  }

private:
  Envoy::Http::ConnectionPool::Cancellable* upstream_http_handle_{};
  Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callbacks_;
  std::shared_ptr<HttpUpstream> http_upstream_{};
  bool has_failure_{false};
};

} // namespace TcpProxy
} // namespace Envoy
#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/http/codec_wrappers.h"
#include "source/common/network/filter_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {

/**
 * Callbacks specific to a codec client.
 */
class CodecClientCallbacks {
public:
  virtual ~CodecClientCallbacks() = default;

  // Called in onPreDecodeComplete
  virtual void onStreamPreDecodeComplete() {}

  /**
   * Called every time an owned stream is destroyed, whether complete or not.
   */
  virtual void onStreamDestroy() PURE;

  /**
   * Called when a stream is reset by the client.
   * @param reason supplies the reset reason.
   */
  virtual void onStreamReset(StreamResetReason reason) PURE;
};

/**
 * This is an HTTP client that multiple stream management and underlying connection management
 * across multiple HTTP codec types.
 */
class CodecClient : protected Logger::Loggable<Logger::Id::client>,
                    public Http::ConnectionCallbacks,
                    public Network::ConnectionCallbacks,
                    public Event::DeferredDeletable {
public:
  /**
   * Type of HTTP codec to use.
   */
  // This is a legacy alias.
  using Type = Envoy::Http::CodecType;

  /**
   * Add a connection callback to the underlying network connection.
   */
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) {
    connection_->addConnectionCallbacks(cb);
  }

  /**
   * Return if half-close semantics are enabled on the underlying connection.
   */
  bool isHalfCloseEnabled() { return connection_->isHalfCloseEnabled(); }

  /**
   * Close the underlying network connection. This is immediate and will not attempt to flush any
   * pending write data.
   */
  void close();

  /**
   * Send a codec level go away indication to the peer.
   */
  void goAway() { codec_->goAway(); }

  /**
   * @return the underlying connection ID.
   */
  uint64_t id() const { return connection_->id(); }

  /**
   * @return the underlying codec protocol.
   */
  Protocol protocol() { return codec_->protocol(); }

  /**
   * @return the underlying connection error.
   */
  absl::string_view connectionFailureReason() { return connection_->transportFailureReason(); }

  /**
   * @return size_t the number of outstanding requests that have not completed or been reset.
   */
  size_t numActiveRequests() { return active_requests_.size(); }

  /**
   * Create a new stream. Note: The CodecClient will NOT buffer multiple requests for HTTP1
   * connections. Thus, calling newStream() before the previous request has been fully encoded
   * is an error. Pipelining is supported however.
   * @param response_decoder supplies the decoder to use for response callbacks.
   * @return StreamEncoder& the encoder to use for encoding the request.
   */
  RequestEncoder& newStream(ResponseDecoder& response_decoder);

  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_->setConnectionStats(stats);
  }

  void setCodecClientCallbacks(CodecClientCallbacks& callbacks) {
    codec_client_callbacks_ = &callbacks;
  }

  void setCodecConnectionCallbacks(Http::ConnectionCallbacks& callbacks) {
    codec_callbacks_ = &callbacks;
  }

  bool remoteClosed() const { return remote_closed_; }

  CodecType type() const { return type_; }

  // Note this is the L4 stream info, not L7.
  StreamInfo::StreamInfo& streamInfo() { return connection_->streamInfo(); }

  /**
   * Connect to the host.
   * Needs to be called after codec_ is instantiated.
   */
  void connect();

protected:
  /**
   * Create a codec client and connect to a remote host/port.
   * @param type supplies the codec type.
   * @param connection supplies the connection to communicate on.
   * @param host supplies the owning host.
   */
  CodecClient(CodecType type, Network::ClientConnectionPtr&& connection,
              Upstream::HostDescriptionConstSharedPtr host, Event::Dispatcher& dispatcher);

  // Http::ConnectionCallbacks
  void onGoAway(GoAwayErrorCode error_code) override {
    if (codec_callbacks_) {
      codec_callbacks_->onGoAway(error_code);
    }
  }
  void onSettings(ReceivedSettings& settings) override {
    if (codec_callbacks_) {
      codec_callbacks_->onSettings(settings);
    }
  }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    if (codec_callbacks_) {
      codec_callbacks_->onMaxStreamsChanged(num_streams);
    }
  }

  void onIdleTimeout() {
    host_->cluster().trafficStats().upstream_cx_idle_timeout_.inc();
    close();
  }

  void disableIdleTimer() {
    if (idle_timer_ != nullptr) {
      idle_timer_->disableTimer();
    }
  }

  void enableIdleTimer() {
    if (idle_timer_ != nullptr) {
      idle_timer_->enableTimer(idle_timeout_.value());
    }
  }

  const CodecType type_;
  // The order of host_, connection_, and codec_ matter as during destruction each can refer to
  // the previous, at least in tests.
  Upstream::HostDescriptionConstSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  ClientConnectionPtr codec_;
  Event::TimerPtr idle_timer_;
  const absl::optional<std::chrono::milliseconds> idle_timeout_;

private:
  /**
   * Wrapper read filter to drive incoming connection data into the codec. We could potentially
   * support other filters in the future.
   */
  struct CodecReadFilter : public Network::ReadFilterBaseImpl {
    CodecReadFilter(CodecClient& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      parent_.onData(data);
      if (end_stream && parent_.isHalfCloseEnabled()) {
        // Note that this results in the connection closed as if it was closed
        // locally, it would be more correct to convey the end stream to the
        // response decoder, but it would require some refactoring.
        parent_.close();
      }
      return Network::FilterStatus::StopIteration;
    }

    CodecClient& parent_;
  };

  struct ActiveRequest;

  /**
   * Wrapper for an outstanding request. Designed for handling stream multiplexing.
   */
  struct ActiveRequest : LinkedObject<ActiveRequest>,
                         public Event::DeferredDeletable,
                         public StreamCallbacks,
                         public ResponseDecoderWrapper,
                         public RequestEncoderWrapper {
    ActiveRequest(CodecClient& parent, ResponseDecoder& inner)
        : ResponseDecoderWrapper(inner), RequestEncoderWrapper(nullptr), parent_(parent) {
      switch (parent.protocol()) {
      case Protocol::Http10:
      case Protocol::Http11:
        // HTTP/1.1 codec does not support half-close on the response completion.
        wait_encode_complete_ = false;
        break;
      case Protocol::Http2:
      case Protocol::Http3:
        wait_encode_complete_ =
            Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_response_half_close");
        break;
      }
    }

    // StreamCallbacks
    void onResetStream(StreamResetReason reason, absl::string_view) override {
      parent_.onReset(*this, reason);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // StreamDecoderWrapper
    void onPreDecodeComplete() override { parent_.responsePreDecodeComplete(*this); }
    void onDecodeComplete() override {}

    // RequestEncoderWrapper
    void onEncodeComplete() override { parent_.requestEncodeComplete(*this); }

    void setEncoder(RequestEncoder& encoder) {
      inner_encoder_ = &encoder;
      inner_encoder_->getStream().addCallbacks(*this);
    }

    void removeEncoderCallbacks() { inner_encoder_->getStream().removeCallbacks(*this); }

    CodecClient& parent_;
    bool wait_encode_complete_{true};
    bool encode_complete_{false};
    bool decode_complete_{false};
  };

  using ActiveRequestPtr = std::unique_ptr<ActiveRequest>;

  /**
   * Called when a response finishes decoding. This is called *before* forwarding on to the
   * wrapped decoder.
   */
  void responsePreDecodeComplete(ActiveRequest& request);
  void requestEncodeComplete(ActiveRequest& request);
  void completeRequest(ActiveRequest& request);

  void deleteRequest(ActiveRequest& request);
  void onReset(ActiveRequest& request, StreamResetReason reason);
  void onData(Buffer::Instance& data);

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  // Pass watermark events from the connection on to the codec which will pass it to the underlying
  // streams.
  void onAboveWriteBufferHighWatermark() override {
    codec_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }
  void onBelowWriteBufferLowWatermark() override {
    codec_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

  std::list<ActiveRequestPtr> active_requests_;
  Http::ConnectionCallbacks* codec_callbacks_{};
  CodecClientCallbacks* codec_client_callbacks_{};
  bool connected_{};
  bool remote_closed_{};
  bool protocol_error_{false};
  bool connect_called_{false};
};

using CodecClientPtr = std::unique_ptr<CodecClient>;

/**
 * Production implementation that installs a real codec without automatically connecting.
 * TODO(danzh) deprecate this class and make CodecClientProd to have the option to defer connect
 * once "envoy.reloadable_features.postpone_h3_client_connect_to_next_loop" is deprecated.
 */
class NoConnectCodecClientProd : public CodecClient {
public:
  NoConnectCodecClientProd(CodecType type, Network::ClientConnectionPtr&& connection,
                           Upstream::HostDescriptionConstSharedPtr host,
                           Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                           const Network::TransportSocketOptionsConstSharedPtr& options);
};

/**
 * Production implementation that installs a real codec.
 */
class CodecClientProd : public NoConnectCodecClientProd {
public:
  CodecClientProd(CodecType type, Network::ClientConnectionPtr&& connection,
                  Upstream::HostDescriptionConstSharedPtr host, Event::Dispatcher& dispatcher,
                  Random::RandomGenerator& random_generator,
                  const Network::TransportSocketOptionsConstSharedPtr& options);
};

} // namespace Http
} // namespace Envoy

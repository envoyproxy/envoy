#pragma once

#include "codec_wrappers.h"

#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/network/filter_impl.h"

namespace Http {

/**
 * All stats for the codec client. @see stats_macros.h
 */
#define ALL_CODEC_CLIENT_STATS(COUNTER) COUNTER(upstream_cx_protocol_error)

/**
 * Definition of all stats for the codec client. @see stats_macros.h
 */
struct CodecClientStats {
  ALL_CODEC_CLIENT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Callbacks specific to a codec client.
 */
class CodecClientCallbacks {
public:
  virtual ~CodecClientCallbacks() {}

  /**
   * Called every time an owned stream is destroyed, whether complete or not.
   */
  virtual void onStreamDestroy() PURE;

  /**
   * Called when a stream is reset by the client.
   * @param reason supplies the reset reason.
   */
  virtual void onStreamReset(Http::StreamResetReason reason) PURE;
};

/**
 * This is an HTTP client that multiple stream management and underlying connection management
 * across multiple HTTP codec types.
 */
class CodecClient : Logger::Loggable<Logger::Id::client>,
                    public Http::ConnectionCallbacks,
                    public Network::ConnectionCallbacks,
                    public Event::DeferredDeletable {
public:
  /**
   * Type of HTTP codec to use.
   */
  enum class Type { HTTP1, HTTP2 };

  ~CodecClient();

  /**
   * Add a connection callback to the underlying network connection.
   */
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) {
    connection_->addConnectionCallbacks(cb);
  }

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
  uint64_t id() { return connection_->id(); }

  /**
   * @return size_t the number of oustanding requests that have not completed or been reset.
   */
  size_t numActiveRequests() { return active_requests_.size(); }

  /**
   * Create a new stream. Note: The CodecClient will NOT buffer multiple requests for HTTP1
   * connections. Thus, calling newStream() before the previous request has been fully encoded
   * is an error. Pipelining is supported however.
   * @param response_decoder supplies the decoder to use for response callbacks.
   * @return Http::StreamEncoder& the encoder to use for encoding the request.
   */
  Http::StreamEncoder& newStream(Http::StreamDecoder& response_decoder);

  void setCodecClientCallbacks(CodecClientCallbacks& callbacks) {
    codec_client_callbacks_ = &callbacks;
  }

  void setCodecConnectionCallbacks(Http::ConnectionCallbacks& callbacks) {
    codec_callbacks_ = &callbacks;
  }

protected:
  /**
   * Create a codec client and connect to a remote host/port.
   * @param type supplies the codec type.
   * @param connection supplies the connection to communicate on.
   * @param stats supplies stats to use for this client.
   */
  CodecClient(Type type, Network::ClientConnectionPtr&& connection, const CodecClientStats& stats);

  // Http::ConnectionCallbacks
  void onGoAway() override {
    if (codec_callbacks_) {
      codec_callbacks_->onGoAway();
    }
  }

  const Type type_;
  Http::ClientConnectionPtr codec_;
  Network::ClientConnectionPtr connection_;

private:
  /**
   * Wrapper read filter to drive incoming connection data into the codec. We could potentially
   * support other filters in the future.
   */
  struct CodecReadFilter : public Network::ReadFilterBaseImpl {
    CodecReadFilter(CodecClient& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    CodecClient& parent_;
  };

  struct ActiveRequest;

  /**
   * Wrapper for the client response decoder. We use this only for managing end of stream.
   */
  struct ResponseDecoderWrapper : public StreamDecoderWrapper {
    ResponseDecoderWrapper(Http::StreamDecoder& inner, ActiveRequest& parent)
        : StreamDecoderWrapper(inner), parent_(parent) {}

    // StreamDecoderWrapper
    void onPreDecodeComplete() override { parent_.parent_.responseDecodeComplete(parent_); }
    void onDecodeComplete() override {}

    ActiveRequest& parent_;
  };

  typedef std::unique_ptr<ResponseDecoderWrapper> ResponseDecoderWrapperPtr;

  /**
   * Wrapper for an outstanding request. Designed for handling stream multiplexing.
   */
  struct ActiveRequest : LinkedObject<ActiveRequest>,
                         public Event::DeferredDeletable,
                         public Http::StreamCallbacks {
    ActiveRequest(CodecClient& parent) : parent_(parent) {}

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override { parent_.onReset(*this, reason); }

    Http::StreamEncoder* encoder_{};
    ResponseDecoderWrapperPtr decoder_wrapper_;
    CodecClient& parent_;
  };

  typedef std::unique_ptr<ActiveRequest> ActiveRequestPtr;

  /**
   * Called when a response finishes decoding. This is called *before* forwarding on to the
   * wrapped decoder.
   */
  void responseDecodeComplete(ActiveRequest& request);

  void deleteRequest(ActiveRequest& request);
  void onReset(ActiveRequest& request, Http::StreamResetReason reason);
  void onData(Buffer::Instance& data);

  // Network::ConnectionCallbacks
  void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}
  void onEvent(uint32_t events) override;

  std::list<ActiveRequestPtr> active_requests_;
  CodecClientStats stats_;
  Http::ConnectionCallbacks* codec_callbacks_{};
  CodecClientCallbacks* codec_client_callbacks_{};
  bool connected_{};
};

typedef std::unique_ptr<CodecClient> CodecClientPtr;

/**
 * Production implementation that installs a real codec.
 */
class CodecClientProd : public CodecClient {
public:
  CodecClientProd(Type type, Network::ClientConnectionPtr&& connection,
                  const CodecClientStats& stats, Stats::Store& store, uint64_t codec_options);
};

} // Http

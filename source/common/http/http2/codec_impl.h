#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optional.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/http/codec_helper.h"
#include "common/http/header_map_impl.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

const std::string ALPN_STRING = "h2";

// This is not the full client magic, but it's the smallest size that should be able to
// differentiate between HTTP/1 and HTTP/2.
const std::string CLIENT_MAGIC_PREFIX = "PRI * HTTP/2";

/**
 * All stats for the HTTP/2 codec. @see stats_macros.h
 */
// clang-format off
#define ALL_HTTP2_CODEC_STATS(COUNTER)                                                             \
  COUNTER(rx_reset)                                                                                \
  COUNTER(tx_reset)                                                                                \
  COUNTER(header_overflow)                                                                         \
  COUNTER(trailers)                                                                                \
  COUNTER(headers_cb_no_stream)
// clang-format on

/**
 * Wrapper struct for the HTTP/2 codec stats. @see stats_macros.h
 */
struct CodecStats {
  ALL_HTTP2_CODEC_STATS(GENERATE_COUNTER_STRUCT)
};

class Utility {
public:
  /**
   * Deal with https://tools.ietf.org/html/rfc7540#section-8.1.2.5
   * @param key supplies the incoming header key.
   * @param value supplies the incoming header value.
   * @param cookies supplies the header string to fill if this is a cookie header that needs to be
   *                rebuilt.
   */
  static bool reconstituteCrumbledCookies(const HeaderString& key, const HeaderString& value,
                                          HeaderString& cookies);
};

/**
 * Base class for HTTP/2 client and server codecs.
 */
class ConnectionImpl : public virtual Connection, protected Logger::Loggable<Logger::Id::http2> {
public:
  ConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                 const Http2Settings& http2_settings)
      : stats_{ALL_HTTP2_CODEC_STATS(POOL_COUNTER_PREFIX(stats, "http2."))},
        connection_(connection),
        per_stream_buffer_limit_(http2_settings.initial_stream_window_size_), dispatching_(false),
        raised_goaway_(false), pending_deferred_reset_(false) {}

  ~ConnectionImpl();

  // Http::Connection
  void dispatch(Buffer::Instance& data) override;
  void goAway() override;
  Protocol protocol() override { return Protocol::Http2; }
  void shutdownNotice() override;
  bool wantsToWrite() override { return nghttp2_session_want_write(session_); }
  // Propogate network connection watermark events to each stream on the connection.
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    for (auto& stream : active_streams_) {
      stream->runHighWatermarkCallbacks();
    }
  }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    for (auto& stream : active_streams_) {
      stream->runLowWatermarkCallbacks();
    }
  }

protected:
  /**
   * Wrapper for static nghttp2 callback dispatchers.
   */
  class Http2Callbacks {
  public:
    Http2Callbacks();
    ~Http2Callbacks();

    const nghttp2_session_callbacks* callbacks() { return callbacks_; }

  private:
    nghttp2_session_callbacks* callbacks_;
  };

  /**
   * Wrapper for static nghttp2 session options.
   */
  class Http2Options {
  public:
    Http2Options();
    ~Http2Options();

    const nghttp2_option* options() { return options_; }

  private:
    nghttp2_option* options_;
  };

  /**
   * Base class for client and server side streams.
   */
  struct StreamImpl : public StreamEncoder,
                      public Stream,
                      public LinkedObject<StreamImpl>,
                      public Event::DeferredDeletable,
                      public StreamCallbackHelper {

    StreamImpl(ConnectionImpl& parent, uint32_t buffer_limit);

    StreamImpl* base() { return this; }
    ssize_t onDataSourceRead(uint64_t length, uint32_t* data_flags);
    int onDataSourceSend(const uint8_t* framehd, size_t length);
    void resetStreamWorker(StreamResetReason reason);
    static void buildHeaders(std::vector<nghttp2_nv>& final_headers, const HeaderMap& headers);
    void saveHeader(HeaderString&& name, HeaderString&& value);
    virtual void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                               nghttp2_data_provider* provider) PURE;
    void submitTrailers(const HeaderMap& trailers);

    // Http::StreamEncoder
    void encode100ContinueHeaders(const HeaderMap& headers) override;
    void encodeHeaders(const HeaderMap& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(const HeaderMap& trailers) override;
    Stream& getStream() override { return *this; }

    // Http::Stream
    void addCallbacks(StreamCallbacks& callbacks) override { addCallbacks_(callbacks); }
    void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
    void resetStream(StreamResetReason reason) override;
    virtual void readDisable(bool disable) override;
    virtual uint32_t bufferLimit() override { return pending_recv_data_.highWatermark(); }

    void setWriteBufferWatermarks(uint32_t low_watermark, uint32_t high_watermark) {
      pending_recv_data_.setWatermarks(low_watermark, high_watermark);
      pending_send_data_.setWatermarks(low_watermark, high_watermark);
    }

    // If the receive buffer encounters watermark callbacks, enable/disable reads on this stream.
    void pendingRecvBufferHighWatermark();
    void pendingRecvBufferLowWatermark();

    // If the send buffer encounters watermark callbacks, propogate this information to the streams.
    // The router and connection manager will propogate them on as appropriate.
    void pendingSendBufferHighWatermark();
    void pendingSendBufferLowWatermark();

    // Max header size of 63K. This is arbitrary but makes it easier to test since nghttp2 doesn't
    // appear to transmit headers greater than approximtely 64K (NGHTTP2_MAX_HEADERSLEN) for reasons
    // I don't fully understand.
    static const uint64_t MAX_HEADER_SIZE = 63 * 1024;

    bool buffers_overrun() const { return read_disable_count_ > 0; }

    ConnectionImpl& parent_;
    HeaderMapImplPtr headers_;
    StreamDecoder* decoder_{};
    int32_t stream_id_{-1};
    uint32_t unconsumed_bytes_{0};
    uint32_t read_disable_count_{0};
    Buffer::WatermarkBuffer pending_recv_data_{
        [this]() -> void { this->pendingRecvBufferLowWatermark(); },
        [this]() -> void { this->pendingRecvBufferHighWatermark(); }};
    Buffer::WatermarkBuffer pending_send_data_{
        [this]() -> void { this->pendingSendBufferLowWatermark(); },
        [this]() -> void { this->pendingSendBufferHighWatermark(); }};
    HeaderMapPtr pending_trailers_;
    Optional<StreamResetReason> deferred_reset_;
    HeaderString cookies_;
    bool local_end_stream_sent_ : 1;
    bool remote_end_stream_ : 1;
    bool data_deferred_ : 1;
    bool waiting_for_non_informational_headers_ : 1;
    bool pending_receive_buffer_high_watermark_called_ : 1;
    bool pending_send_buffer_high_watermark_called_ : 1;
  };

  typedef std::unique_ptr<StreamImpl> StreamImplPtr;

  /**
   * Client side stream (request).
   */
  struct ClientStreamImpl : public StreamImpl {
    using StreamImpl::StreamImpl;

    // StreamImpl
    void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                       nghttp2_data_provider* provider) override;
  };

  /**
   * Server side stream (response).
   */
  struct ServerStreamImpl : public StreamImpl {
    using StreamImpl::StreamImpl;

    // StreamImpl
    void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                       nghttp2_data_provider* provider) override;
  };

  ConnectionImpl* base() { return this; }
  StreamImpl* getStream(int32_t stream_id);
  int saveHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value);
  void sendPendingFrames();
  void sendSettings(const Http2Settings& http2_settings, bool disable_push);

  static Http2Callbacks http2_callbacks_;
  static Http2Options http2_options_;

  std::list<StreamImplPtr> active_streams_;
  nghttp2_session* session_{};
  CodecStats stats_;
  Network::Connection& connection_;
  uint32_t per_stream_buffer_limit_;

private:
  virtual ConnectionCallbacks& callbacks() PURE;
  virtual int onBeginHeaders(const nghttp2_frame* frame) PURE;
  int onData(int32_t stream_id, const uint8_t* data, size_t len);
  int onFrameReceived(const nghttp2_frame* frame);
  int onFrameSend(const nghttp2_frame* frame);
  virtual int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) PURE;
  int onInvalidFrame(int error_code);
  ssize_t onSend(const uint8_t* data, size_t length);
  int onStreamClose(int32_t stream_id, uint32_t error_code);

  bool dispatching_ : 1;
  bool raised_goaway_ : 1;
  bool pending_deferred_reset_ : 1;
};

/**
 * HTTP/2 client connection codec.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks& callbacks,
                       Stats::Scope& stats, const Http2Settings& http2_settings);

  // Http::ClientConnection
  Http::StreamEncoder& newStream(StreamDecoder& response_decoder) override;

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;

  Http::ConnectionCallbacks& callbacks_;
};

/**
 * HTTP/2 server connection codec.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                       Stats::Scope& scope, const Http2Settings& http2_settings);

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;

  ServerConnectionCallbacks& callbacks_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy

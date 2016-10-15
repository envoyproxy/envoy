#pragma once

#include "envoy/common/optional.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"

#include "nghttp2/nghttp2.h"

namespace Http {
namespace Http2 {

const std::string ALPN_STRING = "h2";
const std::string PROTOCOL_STRING = "HTTP/2";

// This is not the full client magic, but it's the smallest size that should be able to
// differentiate between HTTP/1 and HTTP/2.
const std::string CLIENT_MAGIC_PREFIX = "PRI * HTTP/2";

/**
 * All stats for the http/2 codec. @see stats_macros.h
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
 * Wrapper struct for the http/2 codec stats. @see stats_macros.h
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
class ConnectionImpl : public virtual Connection, Logger::Loggable<Logger::Id::http2> {
public:
  ConnectionImpl(Network::Connection& connection, Stats::Store& stats)
      : stats_{ALL_HTTP2_CODEC_STATS(POOL_COUNTER_PREFIX(stats, "http2."))},
        connection_(connection) {}

  ~ConnectionImpl();

  // Http::Connection
  void dispatch(Buffer::Instance& data) override;
  uint64_t features() override { return CodecFeatures::Multiplexing; }
  void goAway() override;
  const std::string& protocolString() override { return PROTOCOL_STRING; }
  void shutdownNotice() override;
  bool wantsToWrite() override { return nghttp2_session_want_write(session_); }

  static const uint64_t MAX_CONCURRENT_STREAMS = 1024;

protected:
  /**
   * Wrapper for static nghttp2 callback dispatchers.
   */
  class Http2Callbacks {
  public:
    Http2Callbacks();
    ~Http2Callbacks();

    nghttp2_session_callbacks* callbacks() { return callbacks_; }

  private:
    nghttp2_session_callbacks* callbacks_;
  };

  /**
   * Base class for client and server side streams.
   */
  struct StreamImpl : public StreamEncoder,
                      public Stream,
                      public LinkedObject<StreamImpl>,
                      public Event::DeferredDeletable {

    StreamImpl(ConnectionImpl& parent);
    ~StreamImpl();

    StreamImpl* base() { return this; }
    ssize_t onDataSourceRead(size_t length, uint32_t* data_flags);
    int onDataSourceSend(const uint8_t* framehd, size_t length);
    void resetStreamWorker(StreamResetReason reason);
    void runResetCallbacks(StreamResetReason reason);
    void buildHeaders(std::vector<nghttp2_nv>& final_headers, const HeaderMap& headers);
    void saveHeader(HeaderString&& name, HeaderString&& value);
    virtual void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                               nghttp2_data_provider* provider) PURE;
    void submitTrailers(const HeaderMap& trailers);

    // Http::StreamEncoder
    void encodeHeaders(const HeaderMap& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(const HeaderMap& trailers) override;
    Stream& getStream() override { return *this; }

    // Http::Stream
    void addCallbacks(StreamCallbacks& callbacks) override { callbacks_.push_back(&callbacks); }
    void removeCallbacks(Http::StreamCallbacks& callbacks) override {
      callbacks_.remove(&callbacks);
    }
    void resetStream(StreamResetReason reason) override;

    // Max header size of 63K. This is arbitrary but makes it easier to test since nghttp2 doesn't
    // appear to transmit headers greater than approximtely 64K (NGHTTP2_MAX_HEADERSLEN) for reasons
    // I don't fully understand.
    static const uint64_t MAX_HEADER_SIZE = 63 * 1024;

    ConnectionImpl& parent_;
    std::list<StreamCallbacks*> callbacks_{};
    HeaderMapImplPtr headers_;
    StreamDecoder* decoder_{};
    int32_t stream_id_{-1};
    bool local_end_stream_{};
    bool local_end_stream_sent_{};
    bool remote_end_stream_{};
    Buffer::OwnedImpl pending_recv_data_;
    Buffer::OwnedImpl pending_send_data_;
    bool data_deferred_{};
    bool reset_callbacks_run_{};
    HeaderMapPtr pending_trailers_;
    Optional<StreamResetReason> deferred_reset_;
    HeaderString cookies_;
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
  void sendSettings(uint64_t codec_options);

  static Http2Callbacks http2_callbacks_;

  std::list<StreamImplPtr> active_streams_;
  nghttp2_session* session_{};
  CodecStats stats_;

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

  // For now just set all window sizes (stream and connection) to 256MiB. We can adjust later if
  // needed.
  static const uint64_t DEFAULT_WINDOW_SIZE = 256 * 1024 * 1024;

  Network::Connection& connection_;
  bool dispatching_{};
  bool raised_goaway_{};
};

/**
 * HTTP/2 client connection codec.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks& callbacks,
                       Stats::Store& stats, uint64_t codec_options);

  // Http::ClientConnection
  Http::StreamEncoder& newStream(StreamDecoder& response_decoder) override;

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;

  ConnectionCallbacks& callbacks_;
};

/**
 * HTTP/2 server connection codec.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                       Stats::Store& stats, uint64_t codec_options);

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;

  ServerConnectionCallbacks& callbacks_;
};

} // Http2
} // Http

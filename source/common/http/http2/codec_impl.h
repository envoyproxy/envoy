#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/http/codec_helper.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_stats.h"
#include "common/http/http2/metadata_decoder.h"
#include "common/http/http2/metadata_encoder.h"
#include "common/http/status.h"
#include "common/http/utility.h"

#include "absl/types/optional.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// This is not the full client magic, but it's the smallest size that should be able to
// differentiate between HTTP/1 and HTTP/2.
const std::string CLIENT_MAGIC_PREFIX = "PRI * HTTP/2";

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

class ConnectionImpl;

// Abstract nghttp2_session factory. Used to enable injection of factories for testing.
class Nghttp2SessionFactory {
public:
  using ConnectionImplType = ConnectionImpl;
  virtual ~Nghttp2SessionFactory() = default;

  // Returns a new nghttp2_session to be used with |connection|.
  virtual nghttp2_session* create(const nghttp2_session_callbacks* callbacks,
                                  ConnectionImplType* connection,
                                  const nghttp2_option* options) PURE;

  // Initializes the |session|.
  virtual void init(nghttp2_session* session, ConnectionImplType* connection,
                    const envoy::config::core::v3::Http2ProtocolOptions& options) PURE;
};

class ProdNghttp2SessionFactory : public Nghttp2SessionFactory {
public:
  nghttp2_session* create(const nghttp2_session_callbacks* callbacks, ConnectionImpl* connection,
                          const nghttp2_option* options) override;

  void init(nghttp2_session* session, ConnectionImpl* connection,
            const envoy::config::core::v3::Http2ProtocolOptions& options) override;

  // Returns a global factory instance. Note that this is possible because no internal state is
  // maintained; the thread safety of create() and init()'s side effects is guaranteed by Envoy's
  // worker based threading model.
  static ProdNghttp2SessionFactory& get() {
    static ProdNghttp2SessionFactory* instance = new ProdNghttp2SessionFactory();
    return *instance;
  }
};

/**
 * Base class for HTTP/2 client and server codecs.
 */
class ConnectionImpl : public virtual Connection, protected Logger::Loggable<Logger::Id::http2> {
public:
  ConnectionImpl(Network::Connection& connection, CodecStats& stats,
                 const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                 const uint32_t max_headers_kb, const uint32_t max_headers_count);

  ~ConnectionImpl() override;

  // Http::Connection
  // NOTE: the `dispatch` method is also overridden in the ServerConnectionImpl class
  Http::Status dispatch(Buffer::Instance& data) override;
  void goAway() override;
  Protocol protocol() override { return Protocol::Http2; }
  void shutdownNotice() override;
  bool wantsToWrite() override { return nghttp2_session_want_write(session_); }
  // Propagate network connection watermark events to each stream on the connection.
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

  /**
   * An inner dispatch call that executes the dispatching logic. While exception removal is in
   * migration (#10878), this function may either throw an exception or return an error status.
   * Exceptions are caught and translated to their corresponding statuses in the outer level
   * dispatch.
   * This needs to be virtual so that ServerConnectionImpl can override.
   * TODO(#10878): Remove this when exception removal is complete.
   */
  virtual Http::Status innerDispatch(Buffer::Instance& data);

protected:
  friend class ProdNghttp2SessionFactory;

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
    Http2Options(const envoy::config::core::v3::Http2ProtocolOptions& http2_options);
    ~Http2Options();

    const nghttp2_option* options() { return options_; }

  protected:
    nghttp2_option* options_;
  };

  class ClientHttp2Options : public Http2Options {
  public:
    ClientHttp2Options(const envoy::config::core::v3::Http2ProtocolOptions& http2_options);
  };

  /**
   * Base class for client and server side streams.
   */
  struct StreamImpl : public virtual StreamEncoder,
                      public Stream,
                      public LinkedObject<StreamImpl>,
                      public Event::DeferredDeletable,
                      public StreamCallbackHelper {

    StreamImpl(ConnectionImpl& parent, uint32_t buffer_limit);
    ~StreamImpl() override;
    // TODO(mattklein123): Optimally this would be done in the destructor but there are currently
    // deferred delete lifetime issues that need sorting out if the destructor of the stream is
    // going to be able to refer to the parent connection.
    void destroy();
    void disarmStreamIdleTimer() {
      if (stream_idle_timer_ != nullptr) {
        // To ease testing and the destructor assertion.
        stream_idle_timer_->disableTimer();
        stream_idle_timer_.reset();
      }
    }

    StreamImpl* base() { return this; }
    ssize_t onDataSourceRead(uint64_t length, uint32_t* data_flags);
    int onDataSourceSend(const uint8_t* framehd, size_t length);
    void resetStreamWorker(StreamResetReason reason);
    static void buildHeaders(std::vector<nghttp2_nv>& final_headers, const HeaderMap& headers);
    void saveHeader(HeaderString&& name, HeaderString&& value);
    void encodeHeadersBase(const std::vector<nghttp2_nv>& final_headers, bool end_stream);
    virtual void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                               nghttp2_data_provider* provider) PURE;
    void encodeTrailersBase(const HeaderMap& headers);
    void submitTrailers(const HeaderMap& trailers);
    void submitMetadata(uint8_t flags);
    virtual StreamDecoder& decoder() PURE;
    virtual HeaderMap& headers() PURE;
    virtual void allocTrailers() PURE;
    virtual HeaderMapPtr cloneTrailers(const HeaderMap& trailers) PURE;
    virtual void createPendingFlushTimer() PURE;
    void onPendingFlushTimer();

    // Http::StreamEncoder
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    Stream& getStream() override { return *this; }
    void encodeMetadata(const MetadataMapVector& metadata_map_vector) override;
    Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return absl::nullopt; }

    // Http::Stream
    void addCallbacks(StreamCallbacks& callbacks) override { addCallbacksHelper(callbacks); }
    void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacksHelper(callbacks); }
    void resetStream(StreamResetReason reason) override;
    void readDisable(bool disable) override;
    uint32_t bufferLimit() override { return pending_recv_data_.highWatermark(); }
    const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override {
      return parent_.connection_.localAddress();
    }
    absl::string_view responseDetails() override { return details_; }
    void setFlushTimeout(std::chrono::milliseconds timeout) override {
      stream_idle_timeout_ = timeout;
    }

    // This code assumes that details is a static string, so that we
    // can avoid copying it.
    void setDetails(absl::string_view details) {
      // It is probably a mistake to call setDetails() twice, so
      // assert that details_ is empty.
      ASSERT(details_.empty());

      details_ = details;
    }

    void setWriteBufferWatermarks(uint32_t low_watermark, uint32_t high_watermark) {
      pending_recv_data_.setWatermarks(low_watermark, high_watermark);
      pending_send_data_.setWatermarks(low_watermark, high_watermark);
    }

    // If the receive buffer encounters watermark callbacks, enable/disable reads on this stream.
    void pendingRecvBufferHighWatermark();
    void pendingRecvBufferLowWatermark();

    // If the send buffer encounters watermark callbacks, propagate this information to the streams.
    // The router and connection manager will propagate them on as appropriate.
    void pendingSendBufferHighWatermark();
    void pendingSendBufferLowWatermark();

    // Does any necessary WebSocket/Upgrade conversion, then passes the headers
    // to the decoder_.
    virtual void decodeHeaders() PURE;
    virtual void decodeTrailers() PURE;

    // Get MetadataEncoder for this stream.
    MetadataEncoder& getMetadataEncoder();
    // Get MetadataDecoder for this stream.
    MetadataDecoder& getMetadataDecoder();
    // Callback function for MetadataDecoder.
    void onMetadataDecoded(MetadataMapPtr&& metadata_map_ptr);

    bool buffersOverrun() const { return read_disable_count_ > 0; }

    ConnectionImpl& parent_;
    int32_t stream_id_{-1};
    uint32_t unconsumed_bytes_{0};
    uint32_t read_disable_count_{0};
    Buffer::WatermarkBuffer pending_recv_data_{
        [this]() -> void { this->pendingRecvBufferLowWatermark(); },
        [this]() -> void { this->pendingRecvBufferHighWatermark(); },
        []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }};
    Buffer::WatermarkBuffer pending_send_data_{
        [this]() -> void { this->pendingSendBufferLowWatermark(); },
        [this]() -> void { this->pendingSendBufferHighWatermark(); },
        []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }};
    HeaderMapPtr pending_trailers_to_encode_;
    std::unique_ptr<MetadataDecoder> metadata_decoder_;
    std::unique_ptr<MetadataEncoder> metadata_encoder_;
    absl::optional<StreamResetReason> deferred_reset_;
    HeaderString cookies_;
    bool local_end_stream_sent_ : 1;
    bool remote_end_stream_ : 1;
    bool data_deferred_ : 1;
    bool received_noninformational_headers_ : 1;
    bool pending_receive_buffer_high_watermark_called_ : 1;
    bool pending_send_buffer_high_watermark_called_ : 1;
    bool reset_due_to_messaging_error_ : 1;
    absl::string_view details_;
    // See HttpConnectionManager.stream_idle_timeout.
    std::chrono::milliseconds stream_idle_timeout_{};
    Event::TimerPtr stream_idle_timer_;
  };

  using StreamImplPtr = std::unique_ptr<StreamImpl>;

  /**
   * Client side stream (request).
   */
  struct ClientStreamImpl : public StreamImpl, public RequestEncoder {
    ClientStreamImpl(ConnectionImpl& parent, uint32_t buffer_limit,
                     ResponseDecoder& response_decoder)
        : StreamImpl(parent, buffer_limit), response_decoder_(response_decoder),
          headers_or_trailers_(ResponseHeaderMapImpl::create()) {}

    // StreamImpl
    void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                       nghttp2_data_provider* provider) override;
    StreamDecoder& decoder() override { return response_decoder_; }
    void decodeHeaders() override;
    void decodeTrailers() override;
    HeaderMap& headers() override {
      if (absl::holds_alternative<ResponseHeaderMapPtr>(headers_or_trailers_)) {
        return *absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
      } else {
        return *absl::get<ResponseTrailerMapPtr>(headers_or_trailers_);
      }
    }
    void allocTrailers() override {
      // If we are waiting for informational headers, make a new response header map, otherwise
      // we are about to receive trailers. The codec makes sure this is the only valid sequence.
      if (received_noninformational_headers_) {
        headers_or_trailers_.emplace<ResponseTrailerMapPtr>(ResponseTrailerMapImpl::create());
      } else {
        headers_or_trailers_.emplace<ResponseHeaderMapPtr>(ResponseHeaderMapImpl::create());
      }
    }
    HeaderMapPtr cloneTrailers(const HeaderMap& trailers) override {
      return createHeaderMap<RequestTrailerMapImpl>(trailers);
    }
    void createPendingFlushTimer() override {
      // Client streams do not create a flush timer because we currently assume that any failure
      // to flush would be covered by a request/stream/etc. timeout.
    }

    // RequestEncoder
    void encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override;
    void encodeTrailers(const RequestTrailerMap& trailers) override {
      encodeTrailersBase(trailers);
    }

    ResponseDecoder& response_decoder_;
    absl::variant<ResponseHeaderMapPtr, ResponseTrailerMapPtr> headers_or_trailers_;
    std::string upgrade_type_;
  };

  using ClientStreamImplPtr = std::unique_ptr<ClientStreamImpl>;

  /**
   * Server side stream (response).
   */
  struct ServerStreamImpl : public StreamImpl, public ResponseEncoder {
    ServerStreamImpl(ConnectionImpl& parent, uint32_t buffer_limit)
        : StreamImpl(parent, buffer_limit), headers_or_trailers_(RequestHeaderMapImpl::create()) {}

    // StreamImpl
    void submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                       nghttp2_data_provider* provider) override;
    StreamDecoder& decoder() override { return *request_decoder_; }
    void decodeHeaders() override;
    void decodeTrailers() override;
    HeaderMap& headers() override {
      if (absl::holds_alternative<RequestHeaderMapPtr>(headers_or_trailers_)) {
        return *absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
      } else {
        return *absl::get<RequestTrailerMapPtr>(headers_or_trailers_);
      }
    }
    void allocTrailers() override {
      headers_or_trailers_.emplace<RequestTrailerMapPtr>(RequestTrailerMapImpl::create());
    }
    HeaderMapPtr cloneTrailers(const HeaderMap& trailers) override {
      return createHeaderMap<ResponseTrailerMapImpl>(trailers);
    }
    void createPendingFlushTimer() override;

    // ResponseEncoder
    void encode100ContinueHeaders(const ResponseHeaderMap& headers) override;
    void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
    void encodeTrailers(const ResponseTrailerMap& trailers) override {
      encodeTrailersBase(trailers);
    }

    RequestDecoder* request_decoder_{};
    absl::variant<RequestHeaderMapPtr, RequestTrailerMapPtr> headers_or_trailers_;
  };

  using ServerStreamImplPtr = std::unique_ptr<ServerStreamImpl>;

  ConnectionImpl* base() { return this; }
  // NOTE: Always use non debug nullptr checks against the return value of this function. There are
  // edge cases (such as for METADATA frames) where nghttp2 will issue a callback for a stream_id
  // that is not associated with an existing stream.
  StreamImpl* getStream(int32_t stream_id);
  int saveHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value);
  void sendPendingFrames();
  void sendSettings(const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                    bool disable_push);
  // Callback triggered when the peer's SETTINGS frame is received.
  // NOTE: This is only used for tests.
  virtual void onSettingsForTest(const nghttp2_settings&) {}

  /**
   * Check if header name contains underscore character.
   * Underscore character is allowed in header names by the RFC-7230 and this check is implemented
   * as a security measure due to systems that treat '_' and '-' as interchangeable.
   * The ServerConnectionImpl may drop header or reject request based on the
   * `common_http_protocol_options.headers_with_underscores_action` configuration option in the
   * HttpConnectionManager.
   */
  virtual absl::optional<int> checkHeaderNameForUnderscores(absl::string_view /* header_name */) {
    return absl::nullopt;
  }

  static Http2Callbacks http2_callbacks_;

  std::list<StreamImplPtr> active_streams_;
  nghttp2_session* session_{};
  CodecStats& stats_;
  Network::Connection& connection_;
  const uint32_t max_headers_kb_;
  const uint32_t max_headers_count_;
  uint32_t per_stream_buffer_limit_;
  bool allow_metadata_;
  const bool stream_error_on_invalid_http_messaging_;
  bool flood_detected_;

  // Set if the type of frame that is about to be sent is PING or SETTINGS with the ACK flag set, or
  // RST_STREAM.
  bool is_outbound_flood_monitored_control_frame_ = 0;
  // This counter keeps track of the number of outbound frames of all types (these that were
  // buffered in the underlying connection but not yet written into the socket). If this counter
  // exceeds the `max_outbound_frames_' value the connection is terminated.
  uint32_t outbound_frames_ = 0;
  // Maximum number of outbound frames. Initialized from corresponding http2_protocol_options.
  // Default value is 10000.
  const uint32_t max_outbound_frames_;
  const std::function<void()> frame_buffer_releasor_;
  // This counter keeps track of the number of outbound frames of types PING, SETTINGS and
  // RST_STREAM (these that were buffered in the underlying connection but not yet written into the
  // socket). If this counter exceeds the `max_outbound_control_frames_' value the connection is
  // terminated.
  uint32_t outbound_control_frames_ = 0;
  // Maximum number of outbound frames of types PING, SETTINGS and RST_STREAM. Initialized from
  // corresponding http2_protocol_options. Default value is 1000.
  const uint32_t max_outbound_control_frames_;
  const std::function<void()> control_frame_buffer_releasor_;
  // This counter keeps track of the number of consecutive inbound frames of types HEADERS,
  // CONTINUATION and DATA with an empty payload and no end stream flag. If this counter exceeds
  // the `max_consecutive_inbound_frames_with_empty_payload_` value the connection is terminated.
  uint32_t consecutive_inbound_frames_with_empty_payload_ = 0;
  // Maximum number of consecutive inbound frames of types HEADERS, CONTINUATION and DATA without
  // a payload. Initialized from corresponding http2_protocol_options. Default value is 1.
  const uint32_t max_consecutive_inbound_frames_with_empty_payload_;

  // This counter keeps track of the number of inbound streams.
  uint32_t inbound_streams_ = 0;
  // This counter keeps track of the number of inbound PRIORITY frames. If this counter exceeds
  // the value calculated using this formula:
  //
  //     max_inbound_priority_frames_per_stream_ * (1 + inbound_streams_)
  //
  // the connection is terminated.
  uint64_t inbound_priority_frames_ = 0;
  // Maximum number of inbound PRIORITY frames per stream. Initialized from corresponding
  // http2_protocol_options. Default value is 100.
  const uint32_t max_inbound_priority_frames_per_stream_;

  // This counter keeps track of the number of inbound WINDOW_UPDATE frames. If this counter exceeds
  // the value calculated using this formula:
  //
  //     1 + 2 * (inbound_streams_ +
  //              max_inbound_window_update_frames_per_data_frame_sent_ * outbound_data_frames_)
  //
  // the connection is terminated.
  uint64_t inbound_window_update_frames_ = 0;
  // This counter keeps track of the number of outbound DATA frames.
  uint64_t outbound_data_frames_ = 0;
  // Maximum number of inbound WINDOW_UPDATE frames per outbound DATA frame sent. Initialized
  // from corresponding http2_protocol_options. Default value is 10.
  const uint32_t max_inbound_window_update_frames_per_data_frame_sent_;

  // For the flood mitigation to work the onSend callback must be called once for each outbound
  // frame. This is what the nghttp2 library is doing, however this is not documented. The
  // Http2FloodMitigationTest.* tests in test/integration/http2_integration_test.cc will break if
  // this changes in the future. Also it is important that onSend does not do partial writes, as the
  // nghttp2 library will keep calling this callback to write the rest of the frame.
  ssize_t onSend(const uint8_t* data, size_t length);

private:
  virtual ConnectionCallbacks& callbacks() PURE;
  virtual int onBeginHeaders(const nghttp2_frame* frame) PURE;
  int onData(int32_t stream_id, const uint8_t* data, size_t len);
  int onBeforeFrameReceived(const nghttp2_frame_hd* hd);
  int onFrameReceived(const nghttp2_frame* frame);
  int onBeforeFrameSend(const nghttp2_frame* frame);
  int onFrameSend(const nghttp2_frame* frame);
  int onError(absl::string_view error);
  virtual int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) PURE;
  int onInvalidFrame(int32_t stream_id, int error_code);
  int onStreamClose(int32_t stream_id, uint32_t error_code);
  int onMetadataReceived(int32_t stream_id, const uint8_t* data, size_t len);
  int onMetadataFrameComplete(int32_t stream_id, bool end_metadata);
  ssize_t packMetadata(int32_t stream_id, uint8_t* buf, size_t len);
  // Adds buffer fragment for a new outbound frame to the supplied Buffer::OwnedImpl.
  // Returns true on success or false if outbound queue limits were exceeded.
  bool addOutboundFrameFragment(Buffer::OwnedImpl& output, const uint8_t* data, size_t length);
  virtual void checkOutboundQueueLimits() PURE;
  void incrementOutboundFrameCount(bool is_outbound_flood_monitored_control_frame);
  virtual bool trackInboundFrames(const nghttp2_frame_hd* hd, uint32_t padding_length) PURE;
  virtual bool checkInboundFrameLimits(int32_t stream_id) PURE;
  void releaseOutboundFrame();
  void releaseOutboundControlFrame();

  bool dispatching_ : 1;
  bool raised_goaway_ : 1;
  bool pending_deferred_reset_ : 1;
};

/**
 * HTTP/2 client connection codec.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  using SessionFactory = Nghttp2SessionFactory;
  ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks& callbacks,
                       CodecStats& stats,
                       const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                       const uint32_t max_response_headers_kb,
                       const uint32_t max_response_headers_count,
                       SessionFactory& http2_session_factory);

  // Http::ClientConnection
  RequestEncoder& newStream(ResponseDecoder& response_decoder) override;

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;

  // Presently client connections only perform accounting of outbound frames and do not
  // terminate connections when queue limits are exceeded. The primary reason is the complexity of
  // the clean-up of upstream connections. The clean-up of upstream connection causes RST_STREAM
  // messages to be sent on corresponding downstream connections. This may actually trigger flood
  // mitigation on the downstream connections, which causes an exception to be thrown in the middle
  // of the clean-up loop, leaving resources in a half cleaned up state.
  // TODO(yanavlasov): add flood mitigation for upstream connections as well.
  void checkOutboundQueueLimits() override {}
  bool trackInboundFrames(const nghttp2_frame_hd*, uint32_t) override { return true; }
  bool checkInboundFrameLimits(int32_t) override { return true; }

  Http::ConnectionCallbacks& callbacks_;
};

/**
 * HTTP/2 server connection codec.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                       CodecStats& stats,
                       const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                       const uint32_t max_request_headers_kb,
                       const uint32_t max_request_headers_count,
                       envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                           headers_with_underscores_action);

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  int onBeginHeaders(const nghttp2_frame* frame) override;
  int onHeader(const nghttp2_frame* frame, HeaderString&& name, HeaderString&& value) override;
  void checkOutboundQueueLimits() override;
  bool trackInboundFrames(const nghttp2_frame_hd* hd, uint32_t padding_length) override;
  bool checkInboundFrameLimits(int32_t stream_id) override;
  absl::optional<int> checkHeaderNameForUnderscores(absl::string_view header_name) override;

  // Http::Connection
  // The reason for overriding the dispatch method is to do flood mitigation only when
  // processing data from downstream client. Doing flood mitigation when processing upstream
  // responses makes clean-up tricky, which needs to be improved (see comments for the
  // ClientConnectionImpl::checkOutboundQueueLimits method). The dispatch method on the
  // ServerConnectionImpl objects is called only when processing data from the downstream client in
  // the ConnectionManagerImpl::onData method.
  Http::Status dispatch(Buffer::Instance& data) override;
  Http::Status innerDispatch(Buffer::Instance& data) override;

  ServerConnectionCallbacks& callbacks_;

  // This flag indicates that downstream data is being dispatched and turns on flood mitigation
  // in the checkMaxOutbound*Framed methods.
  bool dispatching_downstream_data_{false};

  // The action to take when a request header name contains underscore characters.
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy

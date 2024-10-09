#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/assert.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/common/thread.h"
#include "source/common/http/codec_helper.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/http2/metadata_decoder.h"
#include "source/common/http/http2/metadata_encoder.h"
#include "source/common/http/http2/protocol_constraints.h"
#include "source/common/http/status.h"
#include "source/common/http/utility.h"

#include "absl/types/optional.h"
#include "absl/types/span.h"

#ifdef ENVOY_NGHTTP2
#include "nghttp2/nghttp2.h"
#endif
#include "quiche/http2/adapter/http2_adapter.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/oghttp2_adapter.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// Types inherited from nghttp2 and preserved in oghttp2
enum ErrorType {
  OGHTTP2_NO_ERROR,
  OGHTTP2_PROTOCOL_ERROR,
  OGHTTP2_INTERNAL_ERROR,
  OGHTTP2_FLOW_CONTROL_ERROR,
  OGHTTP2_SETTINGS_TIMEOUT,
  OGHTTP2_STREAM_CLOSED,
  OGHTTP2_FRAME_SIZE_ERROR,
  OGHTTP2_REFUSED_STREAM,
  OGHTTP2_CANCEL,
  OGHTTP2_COMPRESSION_ERROR,
  OGHTTP2_CONNECT_ERROR,
  OGHTTP2_ENHANCE_YOUR_CALM,
  OGHTTP2_INADEQUATE_SECURITY,
  OGHTTP2_HTTP_1_1_REQUIRED,
};

class Http2CodecImplTestFixture;

// This is not the full client magic, but it's the smallest size that should be able to
// differentiate between HTTP/1 and HTTP/2.
const std::string CLIENT_MAGIC_PREFIX = "PRI * HTTP/2";
constexpr uint64_t H2_FRAME_HEADER_SIZE = 9;

class ReceivedSettingsImpl : public ReceivedSettings {
public:
  explicit ReceivedSettingsImpl(absl::Span<const http2::adapter::Http2Setting> settings);

  // ReceivedSettings
  const absl::optional<uint32_t>& maxConcurrentStreams() const override {
    return concurrent_stream_limit_;
  }

private:
  absl::optional<uint32_t> concurrent_stream_limit_{};
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

class ConnectionImpl;

// Abstract factory. Used to enable injection of factories for testing.
class Http2SessionFactory {
public:
  using ConnectionImplType = ConnectionImpl;
  virtual ~Http2SessionFactory() = default;

  // Returns a new HTTP/2 session to be used with |connection|.
  virtual std::unique_ptr<http2::adapter::Http2Adapter>
  create(ConnectionImplType* connection,
         const http2::adapter::OgHttp2Adapter::Options& options) PURE;

#ifdef ENVOY_NGHTTP2
  // Returns a new HTTP/2 session to be used with |connection|.
  virtual std::unique_ptr<http2::adapter::Http2Adapter> create(ConnectionImplType* connection,
                                                               const nghttp2_option* options) PURE;
#endif

  // Initializes the |session|.
  virtual void init(ConnectionImplType* connection,
                    const envoy::config::core::v3::Http2ProtocolOptions& options) PURE;
};

class ProdNghttp2SessionFactory : public Http2SessionFactory {
public:
  std::unique_ptr<http2::adapter::Http2Adapter>
  create(ConnectionImpl* connection,
         const http2::adapter::OgHttp2Adapter::Options& options) override;

#ifdef ENVOY_NGHTTP2
  std::unique_ptr<http2::adapter::Http2Adapter> create(ConnectionImpl* connection,
                                                       const nghttp2_option* options) override;
#endif

  void init(ConnectionImpl* connection,
            const envoy::config::core::v3::Http2ProtocolOptions& options) override;

  // Returns a global factory instance. Note that this is possible because no
  // internal state is maintained; the thread safety of create() and init()'s
  // side effects is guaranteed by Envoy's worker based threading model.
  static ProdNghttp2SessionFactory& get() {
    static ProdNghttp2SessionFactory* instance = new ProdNghttp2SessionFactory();
    return *instance;
  }
};

/**
 * Base class for HTTP/2 client and server codecs.
 */
class ConnectionImpl : public virtual Connection,
                       protected Logger::Loggable<Logger::Id::http2>,
                       public ScopeTrackedObject {
public:
  ConnectionImpl(Network::Connection& connection, CodecStats& stats,
                 Random::RandomGenerator& random_generator,
                 const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                 const uint32_t max_headers_kb, const uint32_t max_headers_count);

  ~ConnectionImpl() override;

  // Http::Connection
  // NOTE: the `dispatch` method is also overridden in the ServerConnectionImpl class
  Http::Status dispatch(Buffer::Instance& data) override;
  void goAway() override;
  Protocol protocol() override { return Protocol::Http2; }
  void shutdownNotice() override;
  Status protocolErrorForTest(); // Used in tests to simulate errors.
  bool wantsToWrite() override { return adapter_->want_write(); }
  // Propagate network connection watermark events to each stream on the connection.
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    for (auto& stream : active_streams_) {
      stream->runHighWatermarkCallbacks();
    }
  }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

  void setVisitor(std::unique_ptr<http2::adapter::Http2VisitorInterface> visitor) {
    visitor_ = std::move(visitor);
  }

  // ScopeTrackedObject
  OptRef<const StreamInfo::StreamInfo> trackedStream() const override;
  void dumpState(std::ostream& os, int indent_level) const override;

protected:
  friend class ProdNghttp2SessionFactory;

  /**
   * This class handles protocol events from the codec layer.
   */
  class Http2Visitor : public http2::adapter::Http2VisitorInterface {
  public:
    using Http2ErrorCode = http2::adapter::Http2ErrorCode;
    using Http2PingId = http2::adapter::Http2PingId;
    using Http2Setting = http2::adapter::Http2Setting;
    using Http2StreamId = http2::adapter::Http2StreamId;

    explicit Http2Visitor(ConnectionImpl* connection);

    void setStreamCloseListener(std::function<void(Http2StreamId)> f) {
      stream_close_listener_ = std::move(f);
    }
    int64_t OnReadyToSend(absl::string_view serialized) override;
    DataFrameHeaderInfo OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                   size_t max_length) override;
    bool SendDataFrame(Http2StreamId stream_id, absl::string_view frame_header,
                       size_t payload_bytes) override;
    void OnConnectionError(ConnectionError /*error*/) override {}
    bool OnFrameHeader(Http2StreamId stream_id, size_t length, uint8_t type,
                       uint8_t flags) override;
    void OnSettingsStart() override { settings_.clear(); }
    void OnSetting(Http2Setting setting) override { settings_.push_back(setting); }
    void OnSettingsEnd() override { connection_->onSettings(settings_); }
    void OnSettingsAck() override {}
    bool OnBeginHeadersForStream(Http2StreamId stream_id) override;
    OnHeaderResult OnHeaderForStream(Http2StreamId stream_id, absl::string_view name_view,
                                     absl::string_view value_view) override;
    bool OnEndHeadersForStream(Http2StreamId stream_id) override;
    bool OnDataPaddingLength(Http2StreamId stream_id, size_t padding_length) override;
    bool OnBeginDataForStream(Http2StreamId stream_id, size_t payload_length) override;
    bool OnDataForStream(Http2StreamId stream_id, absl::string_view data) override;
    bool OnEndStream(Http2StreamId stream_id) override;
    void OnRstStream(Http2StreamId stream_id, Http2ErrorCode error_code) override;
    bool OnCloseStream(Http2StreamId stream_id, Http2ErrorCode error_code) override;
    void OnPriorityForStream(Http2StreamId /*stream_id*/, Http2StreamId /*parent_stream_id*/,
                             int /*weight*/, bool /*exclusive*/) override {}
    void OnPing(Http2PingId ping_id, bool is_ack) override;
    void OnPushPromiseForStream(Http2StreamId /*stream_id*/,
                                Http2StreamId /*promised_stream_id*/) override {}
    bool OnGoAway(Http2StreamId last_accepted_stream_id, Http2ErrorCode error_code,
                  absl::string_view opaque_data) override;
    void OnWindowUpdate(Http2StreamId /*stream_id*/, int /*window_increment*/) override {}
    int OnBeforeFrameSent(uint8_t frame_type, Http2StreamId stream_id, size_t length,
                          uint8_t flags) override;
    int OnFrameSent(uint8_t frame_type, Http2StreamId stream_id, size_t length, uint8_t flags,
                    uint32_t error_code) override;
    bool OnInvalidFrame(Http2StreamId stream_id, InvalidFrameError error) override;
    void OnBeginMetadataForStream(Http2StreamId /*stream_id*/, size_t /*payload_length*/) override {
    }
    bool OnMetadataForStream(Http2StreamId stream_id, absl::string_view metadata) override;
    bool OnMetadataEndForStream(Http2StreamId stream_id) override;
    void OnErrorDebug(absl::string_view message) override;

  private:
    ConnectionImpl* const connection_;
    std::vector<http2::adapter::Http2Setting> settings_;
    struct FrameHeaderInfo {
      Http2StreamId stream_id;
      size_t length;
      uint8_t type;
      uint8_t flags;
    };
    FrameHeaderInfo current_frame_ = {};
    size_t padding_length_ = 0;
    size_t remaining_data_payload_ = 0;
    // TODO: remove when removing `envoy.reloadable_features.http2_use_oghttp2`.
    std::function<void(Http2StreamId)> stream_close_listener_;
  };

  /**
   * Wrapper for static nghttp2 session options.
   */
  class Http2Options {
  public:
    Http2Options(const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                 uint32_t max_headers_kb);
    ~Http2Options();

#ifdef ENVOY_NGHTTP2
    const nghttp2_option* options() { return options_; }
#endif
    const http2::adapter::OgHttp2Adapter::Options& ogOptions() { return og_options_; }

  protected:
#ifdef ENVOY_NGHTTP2
    nghttp2_option* options_;
#endif
    http2::adapter::OgHttp2Adapter::Options og_options_;
  };

  class ClientHttp2Options : public Http2Options {
  public:
    ClientHttp2Options(const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                       uint32_t max_headers_kb);
  };

  /**
   * Base class for client and server side streams.
   */
  struct StreamImpl : public virtual StreamEncoder,
                      public LinkedObject<StreamImpl>,
                      public Event::DeferredDeletable,
                      public Http::MultiplexedStreamImplBase,
                      public ScopeTrackedObject {
    enum class HeadersState {
      Request,
      Response,
      Headers, // Signifies additional headers after the initial request/response set.
    };

    StreamImpl(ConnectionImpl& parent, uint32_t buffer_limit);

    // Http::MultiplexedStreamImplBase
    void destroy() override;
    void onPendingFlushTimer() override;
    CodecEventCallbacks*
    registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) override {
      extend_stream_lifetime_flag_ = true;
      return MultiplexedStreamImplBase::registerCodecEventCallbacks(codec_callbacks);
    }

    StreamImpl* base() { return this; }
    void resetStreamWorker(StreamResetReason reason);
    static std::vector<http2::adapter::Header> buildHeaders(const HeaderMap& headers);
    virtual Status onBeginHeaders() PURE;
    virtual void advanceHeadersState() PURE;
    virtual HeadersState headersState() const PURE;
    void saveHeader(HeaderString&& name, HeaderString&& value);
    void encodeHeadersBase(const HeaderMap& headers, bool end_stream);
    virtual void submitHeaders(const HeaderMap& headers, bool end_stream) PURE;
    void encodeTrailersBase(const HeaderMap& headers);
    void submitTrailers(const HeaderMap& trailers);
    // Returns true if the stream should defer the local reset stream until after the next call to
    // sendPendingFrames so pending outbound frames have one final chance to be flushed. If we
    // submit a reset, nghttp2 will cancel outbound frames that have not yet been sent.
    virtual bool useDeferredReset() const PURE;
    virtual StreamDecoder& decoder() PURE;
    virtual HeaderMap& headers() PURE;
    virtual void allocTrailers() PURE;
    virtual HeaderMapPtr cloneTrailers(const HeaderMap& trailers) PURE;

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
    uint32_t bufferLimit() const override { return pending_recv_data_->highWatermark(); }
    const Network::ConnectionInfoProvider& connectionInfoProvider() override {
      return parent_.connection_.connectionInfoProvider();
    }
    absl::string_view responseDetails() override { return details_; }
    Buffer::BufferMemoryAccountSharedPtr account() const override { return buffer_memory_account_; }
    void setAccount(Buffer::BufferMemoryAccountSharedPtr account) override;

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level) const override;

    // This code assumes that details is a static string, so that we
    // can avoid copying it.
    void setDetails(absl::string_view details) {
      if (details_.empty()) {
        details_ = details;
      }
    }

    void setWriteBufferWatermarks(uint32_t high_watermark) {
      pending_recv_data_->setWatermarks(high_watermark);
      pending_send_data_->setWatermarks(high_watermark);
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
    bool maybeDeferDecodeTrailers();
    // Consumes any decoded data, buffering if backed up.
    void decodeData();

    // Get MetadataEncoder for this stream.
    NewMetadataEncoder& getMetadataEncoder();
    // Get MetadataDecoder for this stream.
    MetadataDecoder& getMetadataDecoder();
    // Callback function for MetadataDecoder.
    void onMetadataDecoded(MetadataMapPtr&& metadata_map_ptr);

    bool buffersOverrun() const { return read_disable_count_ > 0; }
    bool shouldAllowPeerAdditionalStreamWindow() const {
      return !buffersOverrun() && !pending_recv_data_->highWatermarkTriggered();
    }

    void encodeDataHelper(Buffer::Instance& data, bool end_stream,
                          bool skip_encoding_empty_trailers);
    // Called from either process_buffered_data_callback_.
    void processBufferedData();

    // Called when the frame with END_STREAM is sent for this stream.
    void onEndStreamEncoded() {
      if (codec_callbacks_) {
        codec_callbacks_->onCodecEncodeComplete();
      }
    }

    const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }
    ConnectionImpl& parent_;
    int32_t stream_id_{-1};
    uint32_t unconsumed_bytes_{0};
    uint32_t read_disable_count_{0};
    StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};

    Buffer::BufferMemoryAccountSharedPtr buffer_memory_account_;
    // Note that in current implementation the watermark callbacks of the pending_recv_data_ are
    // never called. The watermark value is set to the size of the stream window. As a result this
    // watermark can never overflow because the peer can never send more bytes than the stream
    // window without triggering protocol error. This buffer is drained after each DATA frame was
    // dispatched through the filter chain unless
    // envoy.reloadable_features.defer_processing_backedup_streams is enabled,
    // in which case this buffer may accumulate data.
    // See source/docs/flow_control.md for more information.
    Buffer::InstancePtr pending_recv_data_;
    Buffer::InstancePtr pending_send_data_;
    HeaderMapPtr pending_trailers_to_encode_;
    std::unique_ptr<MetadataDecoder> metadata_decoder_;
    std::unique_ptr<NewMetadataEncoder> metadata_encoder_;
    absl::optional<StreamResetReason> deferred_reset_;
    // Holds the reset reason for this stream. Useful if we have buffered data
    // to determine whether we should continue processing that data.
    absl::optional<StreamResetReason> reset_reason_;
    HeaderString cookies_;
    bool local_end_stream_sent_ : 1;
    bool remote_end_stream_ : 1;
    bool remote_rst_ : 1;
    bool data_deferred_ : 1;
    bool received_noninformational_headers_ : 1;
    bool pending_receive_buffer_high_watermark_called_ : 1;
    bool pending_send_buffer_high_watermark_called_ : 1;
    bool reset_due_to_messaging_error_ : 1;
    bool defer_processing_backedup_streams_ : 1;
    // Latch whether this stream is operating with this flag.
    bool extend_stream_lifetime_flag_ : 1;
    absl::string_view details_;

    /**
     * Tracks buffering that may occur for a stream if it is backed up.
     */
    struct BufferedStreamManager {
      bool body_buffered_{false};
      bool trailers_buffered_{false};

      // We received a call to onStreamClose for the stream, but deferred it
      // as the stream had pending data to process and the stream was not reset.
      bool buffered_on_stream_close_{false};

      // Segment size for processing body data. Defaults to the value of high
      // watermark of the *pending_recv_data_* buffer.
      // If 0, we will process all buffered data.
      uint32_t defer_processing_segment_size_{0};

      bool decodeAsChunks() const { return defer_processing_segment_size_ > 0; }
      bool hasBufferedBodyOrTrailers() const { return body_buffered_ || trailers_buffered_; }
    };

    BufferedStreamManager stream_manager_;
    Event::SchedulableCallbackPtr process_buffered_data_callback_;

  protected:
    // Http::MultiplexedStreamImplBase
    bool hasPendingData() override {
      return pending_send_data_->length() > 0 || pending_trailers_to_encode_ != nullptr;
    }
    bool continueProcessingBufferedData() const {
      // We should stop processing buffered data if either
      // 1) Buffers become overrun
      // 2) The stream ends up getting reset
      // Both of these can end up changing as a result of processing buffered data.
      return !buffersOverrun() && !reset_reason_.has_value();
    }

    // Avoid inversion in the case where we saw trailers, acquiring the
    // remote_end_stream_ being set to true, but the trailers ended up being
    // buffered.
    // All buffered body must be consumed before we send end stream.
    bool sendEndStream() const {
      return remote_end_stream_ && !stream_manager_.trailers_buffered_ &&
             !stream_manager_.body_buffered_;
    }

    // Schedules a callback either in the current or next iteration to process
    // buffered data.
    void scheduleProcessingOfBufferedData(bool schedule_next_iteration);

    // Marks data consumed by the stream, granting the peer additional stream
    // window.
    void grantPeerAdditionalStreamWindow();
  };

  // Encapsulates the logic for sending DATA frames on a given stream.
  // Deprecated. Remove when removing
  // `envoy_reloadable_features_http2_use_visitor_for_data`.
  class StreamDataFrameSource : public http2::adapter::DataFrameSource {
  public:
    explicit StreamDataFrameSource(StreamImpl& stream) : stream_(stream) {}

    // Returns a pair of the next payload length, and whether that payload is the end of the data
    // for this stream.
    std::pair<int64_t, bool> SelectPayloadLength(size_t max_length) override;
    // Queues the frame header and a DATA frame payload of the specified length for writing.
    bool Send(absl::string_view frame_header, size_t payload_length) override;
    // Whether the codec should send the END_STREAM flag on the final DATA frame.
    bool send_fin() const override { return send_fin_; }

  private:
    StreamImpl& stream_;
    bool send_fin_ = false;
  };

  using StreamImplPtr = std::unique_ptr<StreamImpl>;

  /**
   * Client side stream (request).
   */
  struct ClientStreamImpl : public StreamImpl, public RequestEncoder {
    ClientStreamImpl(ConnectionImpl& parent, uint32_t buffer_limit,
                     ResponseDecoder& response_decoder)
        : StreamImpl(parent, buffer_limit), response_decoder_(response_decoder),
          headers_or_trailers_(
              ResponseHeaderMapImpl::create(parent_.max_headers_kb_, parent_.max_headers_count_)) {}

    // Http::MultiplexedStreamImplBase
    // Client streams do not need a flush timer because we currently assume that any failure
    // to flush would be covered by a request/stream/etc. timeout.
    void setFlushTimeout(std::chrono::milliseconds /*timeout*/) override {}
    CodecEventCallbacks* registerCodecEventCallbacks(CodecEventCallbacks*) override {
      ENVOY_BUG(false, "CodecEventCallbacks for HTTP2 client stream unimplemented.");
      return nullptr;
    }
    // StreamImpl
    void submitHeaders(const HeaderMap& headers, bool end_stream) override;
    Status onBeginHeaders() override;
    void advanceHeadersState() override;
    HeadersState headersState() const override { return headers_state_; }
    // Do not use deferred reset on upstream connections.
    bool useDeferredReset() const override { return false; }
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
        headers_or_trailers_.emplace<ResponseTrailerMapPtr>(
            ResponseTrailerMapImpl::create(parent_.max_headers_kb_, parent_.max_headers_count_));
      } else {
        headers_or_trailers_.emplace<ResponseHeaderMapPtr>(
            ResponseHeaderMapImpl::create(parent_.max_headers_kb_, parent_.max_headers_count_));
      }
    }
    HeaderMapPtr cloneTrailers(const HeaderMap& trailers) override {
      return createHeaderMap<RequestTrailerMapImpl>(trailers);
    }

    // RequestEncoder
    Status encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override;
    void encodeTrailers(const RequestTrailerMap& trailers) override {
      encodeTrailersBase(trailers);
    }
    void enableTcpTunneling() override {}

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level) const override;

    ResponseDecoder& response_decoder_;
    absl::variant<ResponseHeaderMapPtr, ResponseTrailerMapPtr> headers_or_trailers_;
    std::string upgrade_type_;
    HeadersState headers_state_ = HeadersState::Response;
  };

  using ClientStreamImplPtr = std::unique_ptr<ClientStreamImpl>;

  /**
   * Server side stream (response).
   */
  struct ServerStreamImpl : public StreamImpl, public ResponseEncoder {
    ServerStreamImpl(ConnectionImpl& parent, uint32_t buffer_limit)
        : StreamImpl(parent, buffer_limit),
          headers_or_trailers_(
              RequestHeaderMapImpl::create(parent_.max_headers_kb_, parent_.max_headers_count_)) {}

    // StreamImpl
    void destroy() override;
    void submitHeaders(const HeaderMap& headers, bool end_stream) override;
    Status onBeginHeaders() override;
    void advanceHeadersState() override;
    HeadersState headersState() const override { return headers_state_; }
    // Enable deferred reset on downstream connections so outbound HTTP internal error replies are
    // written out before force resetting the stream, assuming there is enough H2 connection flow
    // control window is available.
    bool useDeferredReset() const override { return true; }
    StreamDecoder& decoder() override { return *request_decoder_; }
    void decodeHeaders() override;
    void decodeTrailers() override;
    HeaderMap& headers() override {
      if (absl::holds_alternative<RequestHeaderMapSharedPtr>(headers_or_trailers_)) {
        return *absl::get<RequestHeaderMapSharedPtr>(headers_or_trailers_);
      } else {
        return *absl::get<RequestTrailerMapPtr>(headers_or_trailers_);
      }
    }
    void allocTrailers() override {
      headers_or_trailers_.emplace<RequestTrailerMapPtr>(
          RequestTrailerMapImpl::create(parent_.max_headers_kb_, parent_.max_headers_count_));
    }
    HeaderMapPtr cloneTrailers(const HeaderMap& trailers) override {
      return createHeaderMap<ResponseTrailerMapImpl>(trailers);
    }
    void resetStream(StreamResetReason reason) override;

    // ResponseEncoder
    void encode1xxHeaders(const ResponseHeaderMap& headers) override;
    void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
    void encodeTrailers(const ResponseTrailerMap& trailers) override {
      encodeTrailersBase(trailers);
    }
    void setRequestDecoder(Http::RequestDecoder& decoder) override { request_decoder_ = &decoder; }
    void setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr,
                                              Http::ResponseHeaderMapConstSharedPtr,
                                              Http::ResponseTrailerMapConstSharedPtr,
                                              StreamInfo::StreamInfo&) override {}

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level) const override;

    absl::variant<RequestHeaderMapSharedPtr, RequestTrailerMapPtr> headers_or_trailers_;

    bool streamErrorOnInvalidHttpMessage() const override {
      return parent_.stream_error_on_invalid_http_messaging_;
    }

  private:
    RequestDecoder* request_decoder_{};
    HeadersState headers_state_ = HeadersState::Request;
  };

  using ServerStreamImplPtr = std::unique_ptr<ServerStreamImpl>;

  ConnectionImpl* base() { return this; }
  // NOTE: Always use non debug nullptr checks against the return value of this function. There are
  // edge cases (such as for METADATA frames) where nghttp2 will issue a callback for a stream_id
  // that is not associated with an existing stream.
  const StreamImpl* getStream(int32_t stream_id) const;
  StreamImpl* getStream(int32_t stream_id);
  // Same as getStream, but without the ASSERT.
  const StreamImpl* getStreamUnchecked(int32_t stream_id) const;
  StreamImpl* getStreamUnchecked(int32_t stream_id);
  int saveHeader(int32_t stream_id, HeaderString&& name, HeaderString&& value);

  /**
   * Copies any frames pending internally by nghttp2 into outbound buffer.
   * The `sendPendingFrames()` can be called in 3 different contexts:
   * 1. dispatching_ == true, aka the dispatching context. The `sendPendingFrames()` is no-op and
   *    always returns success to avoid reentering nghttp2 library.
   * 2. Server codec only. dispatching_ == false.
   *    The `sendPendingFrames()` returns the status of the protocol constraint checks. Outbound
   *    frame accounting is performed.
   * 3. dispatching_ == false. The `sendPendingFrames()` always returns success. No outbound
   *    frame accounting.
   *
   * TODO(yanavlasov): harmonize behavior for cases 2, 3.
   */
  Status sendPendingFrames();

  /**
   * Call the sendPendingFrames() method and schedule disconnect callback when
   * sendPendingFrames() returns an error.
   * Return true if the disconnect callback has been scheduled.
   */
  bool sendPendingFramesAndHandleError();
  void sendSettings(const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                    bool disable_push);
  void sendSettingsHelper(const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                          bool disable_push);
  // Callback triggered when the peer's SETTINGS frame is received.
  virtual void onSettings(absl::Span<const http2::adapter::Http2Setting> settings) {
    ReceivedSettingsImpl received_settings(settings);
    callbacks().onSettings(received_settings);
  }

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

  /**
   * Save `status` into codec_callback_status_.
   * Return codec callback return code corresponding to `status`.
   */
  int setAndCheckCodecCallbackStatus(Status&& status);

  /**
   * Callback for terminating connection when protocol constrain has been violated
   * outside of the dispatch context.
   */
  void scheduleProtocolConstraintViolationCallback();
  void onProtocolConstraintViolation();

  // Whether to use the new HTTP/2 library.
  bool use_oghttp2_library_;

  // If deferred processing, the streams will be in LRU order based on when the
  // stream encoded to the http2 connection. The LRU property is used when
  // raising low watermark on the http2 connection to prioritize how streams get
  // notified, prefering those that haven't recently written.
  std::list<StreamImplPtr> active_streams_;

  // Tracks the stream id of the current stream we're processing.
  // This should only be set while we're in the context of dispatching to nghttp2.
  absl::optional<int32_t> current_stream_id_;
  std::unique_ptr<http2::adapter::Http2VisitorInterface> visitor_;
  std::unique_ptr<http2::adapter::Http2Adapter> adapter_;

  CodecStats& stats_;
  Network::Connection& connection_;
  const uint32_t max_headers_kb_;
  const uint32_t max_headers_count_;
  uint32_t per_stream_buffer_limit_;
  bool allow_metadata_;
  const bool stream_error_on_invalid_http_messaging_;

  // Status for any errors encountered by the nghttp2 callbacks.
  // nghttp2 library uses single return code to indicate callback failure and
  // `codec_callback_status_` is used to save right error information returned by a callback. The
  // `codec_callback_status_` is valid iff nghttp call returned NGHTTP2_ERR_CALLBACK_FAILURE.
  Status codec_callback_status_;

  // Set if the type of frame that is about to be sent is PING or SETTINGS with the ACK flag set, or
  // RST_STREAM.
  bool is_outbound_flood_monitored_control_frame_ = 0;
  ProtocolConstraints protocol_constraints_;

  // For the flood mitigation to work the onSend callback must be called once for each outbound
  // frame. This is what the nghttp2 library is doing, however this is not documented. The
  // Http2FloodMitigationTest.* tests in test/integration/http2_integration_test.cc will break if
  // this changes in the future. Also it is important that onSend does not do partial writes, as the
  // nghttp2 library will keep calling this callback to write the rest of the frame.
  ssize_t onSend(const uint8_t* data, size_t length);

  // Called when a stream encodes to the http2 connection which enables us to
  // keep the active_streams list in LRU if deferred processing.
  void updateActiveStreamsOnEncode(StreamImpl& stream) {
    if (stream.defer_processing_backedup_streams_) {
      LinkedList::moveIntoList(stream.removeFromList(active_streams_), active_streams_);
    }
  }

  // dumpState helper method.
  virtual void dumpStreams(std::ostream& os, int indent_level) const;

  // Send a keepalive ping, and set the idle timer for ping timeout.
  void sendKeepalive();

  const MonotonicTime& lastReceivedDataTime() { return last_received_data_time_; }

private:
  friend class Http2CodecImplTestFixture;

  virtual ConnectionCallbacks& callbacks() PURE;
  virtual Status onBeginHeaders(int32_t stream_id) PURE;
  int onData(int32_t stream_id, const uint8_t* data, size_t len);
  Status onBeforeFrameReceived(int32_t stream_id, size_t length, uint8_t type, uint8_t flags);
  Status onPing(uint64_t opaque_data, bool is_ack);
  Status onBeginData(int32_t stream_id, size_t length, uint8_t flags, size_t padding);
  Status onGoAway(uint32_t error_code);
  Status onHeaders(int32_t stream_id, size_t length, uint8_t flags);
  Status onRstStream(int32_t stream_id, uint32_t error_code);
  int onBeforeFrameSend(int32_t stream_id, size_t length, uint8_t type, uint8_t flags);
  int onFrameSend(int32_t stream_id, size_t length, uint8_t type, uint8_t flags,
                  uint32_t error_code);
  int onError(absl::string_view error);
  virtual int onHeader(int32_t stream_id, HeaderString&& name, HeaderString&& value) PURE;
  int onInvalidFrame(int32_t stream_id, int error_code);
  // Pass through invoking with the actual stream.
  Status onStreamClose(int32_t stream_id, uint32_t error_code);
  // Should be invoked directly in buffered onStreamClose scenarios
  // where nghttp2 might have already forgotten about the stream.
  Status onStreamClose(StreamImpl* stream, uint32_t error_code);
  int onMetadataReceived(int32_t stream_id, const uint8_t* data, size_t len);
  int onMetadataFrameComplete(int32_t stream_id, bool end_metadata);

  // Adds buffer fragment for a new outbound frame to the supplied Buffer::OwnedImpl.
  void addOutboundFrameFragment(Buffer::OwnedImpl& output, const uint8_t* data, size_t length);
  Status trackInboundFrames(int32_t stream_id, size_t length, uint8_t type, uint8_t flags,
                            uint32_t padding_length);
  void onKeepaliveResponse();
  void onKeepaliveResponseTimeout();
  bool slowContainsStreamId(int32_t stream_id) const;
  virtual StreamResetReason getMessagingErrorResetReason() const PURE;

  // Tracks the current slice we're processing in the dispatch loop.
  const Buffer::RawSlice* current_slice_ = nullptr;
  // Streams that are pending deferred reset. Using an ordered map provides determinism in the rare
  // case where there are multiple streams waiting for deferred reset. The stream id is also used to
  // remove streams from the map when they are closed in order to avoid calls to resetStreamWorker
  // after the stream has been removed from the active list.
  std::map<int32_t, StreamImpl*> pending_deferred_reset_streams_;
  bool dispatching_ : 1;
  bool raised_goaway_ : 1;
  Event::SchedulableCallbackPtr protocol_constraint_violation_callback_;
  Random::RandomGenerator& random_;
  MonotonicTime last_received_data_time_{};
  Event::TimerPtr keepalive_send_timer_;
  Event::TimerPtr keepalive_timeout_timer_;
  std::chrono::milliseconds keepalive_interval_;
  std::chrono::milliseconds keepalive_timeout_;
  uint32_t keepalive_interval_jitter_percent_;
};

/**
 * HTTP/2 client connection codec.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  using SessionFactory = Http2SessionFactory;
  ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks& callbacks,
                       CodecStats& stats, Random::RandomGenerator& random_generator,
                       const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                       const uint32_t max_response_headers_kb,
                       const uint32_t max_response_headers_count,
                       SessionFactory& http2_session_factory);

  // Http::ClientConnection
  RequestEncoder& newStream(ResponseDecoder& response_decoder) override;

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  Status onBeginHeaders(int32_t stream_id) override;
  int onHeader(int32_t stream_id, HeaderString&& name, HeaderString&& value) override;
  void dumpStreams(std::ostream& os, int indent_level) const override;
  StreamResetReason getMessagingErrorResetReason() const override;
  Http::ConnectionCallbacks& callbacks_;
  std::chrono::milliseconds idle_session_requires_ping_interval_;
};

/**
 * HTTP/2 server connection codec.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                       CodecStats& stats, Random::RandomGenerator& random_generator,
                       const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                       const uint32_t max_request_headers_kb,
                       const uint32_t max_request_headers_count,
                       envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                           headers_with_underscores_action,
                       Server::OverloadManager& overload_manager);

private:
  // ConnectionImpl
  ConnectionCallbacks& callbacks() override { return callbacks_; }
  Status onBeginHeaders(int32_t stream_id) override;
  int onHeader(int32_t stream_id, HeaderString&& name, HeaderString&& value) override;
  absl::optional<int> checkHeaderNameForUnderscores(absl::string_view header_name) override;
  StreamResetReason getMessagingErrorResetReason() const override {
    return StreamResetReason::LocalReset;
  }

  // Http::Connection
  // The reason for overriding the dispatch method is to do flood mitigation only when
  // processing data from downstream client. Doing flood mitigation when processing upstream
  // responses makes clean-up tricky, which needs to be improved (see comments for the
  // ClientConnectionImpl::checkProtocolConstraintsStatus method). The dispatch method on the
  // ServerConnectionImpl objects is called only when processing data from the downstream client in
  // the ConnectionManagerImpl::onData method.
  Http::Status dispatch(Buffer::Instance& data) override;

  ServerConnectionCallbacks& callbacks_;

  // The action to take when a request header name contains underscore characters.
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
  Server::LoadShedPoint* should_send_go_away_on_dispatch_{nullptr};
  bool sent_go_away_on_dispatch_{false};
};

} // namespace Http2
} // namespace Http
} // namespace Envoy

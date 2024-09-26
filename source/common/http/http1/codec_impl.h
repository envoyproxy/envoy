#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/assert.h"
#include "source/common/common/statusor.h"
#include "source/common/http/codec_helper.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http1/header_formatter.h"
#include "source/common/http/http1/parser.h"
#include "source/common/http/status.h"

namespace Envoy {
namespace Http {
namespace Http1 {

// The default limit of 80 KiB is the vanilla http_parser behaviour.
inline constexpr uint32_t MAX_RESPONSE_HEADERS_KB = 80;

class ConnectionImpl;

/**
 * Base class for HTTP/1.1 request and response encoders.
 */
class StreamEncoderImpl : public virtual StreamEncoder,
                          public Stream,
                          public Logger::Loggable<Logger::Id::http>,
                          public StreamCallbackHelper,
                          public Http1StreamEncoderOptions {
public:
  ~StreamEncoderImpl() override {
    // When the stream goes away, undo any read blocks to resume reading.
    while (read_disable_calls_ != 0) {
      StreamEncoderImpl::readDisable(false);
    }
  }
  // Http::StreamEncoder
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const MetadataMapVector&) override;
  Stream& getStream() override { return *this; }
  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return *this; }

  // Http::Http1StreamEncoderOptions
  void disableChunkEncoding() override { disable_chunk_encoding_ = true; }

  // Http::Stream
  void addCallbacks(StreamCallbacks& callbacks) override { addCallbacksHelper(callbacks); }
  void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacksHelper(callbacks); }
  CodecEventCallbacks* registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) override;
  // After this is called, for the HTTP/1 codec, the connection should be closed, i.e. no further
  // progress may be made with the codec.
  void resetStream(StreamResetReason reason) override;
  void readDisable(bool disable) override;
  uint32_t bufferLimit() const override;
  absl::string_view responseDetails() override { return details_; }
  const Network::ConnectionInfoProvider& connectionInfoProvider() override;
  void setFlushTimeout(std::chrono::milliseconds) override {
    // HTTP/1 has one stream per connection, thus any data encoded is immediately written to the
    // connection, invoking any watermarks as necessary. There is no internal buffering that would
    // require a flush timeout not already covered by other timeouts.
  }

  Buffer::BufferMemoryAccountSharedPtr account() const override { return buffer_memory_account_; }

  void setAccount(Buffer::BufferMemoryAccountSharedPtr account) override {
    // TODO(kbaichoo): implement account tracking for H1. Particularly, binding
    // the account to the buffers used. The current wiring is minimal, and used
    // to ensure the memory_account gets notified that the downstream request is
    // closing.
    buffer_memory_account_ = account;
  }

  void setIsResponseToHeadRequest(bool value) { is_response_to_head_request_ = value; }
  void setIsResponseToConnectRequest(bool value) { is_response_to_connect_request_ = value; }
  void setDetails(absl::string_view details) { details_ = details; }

  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

protected:
  StreamEncoderImpl(ConnectionImpl& connection, StreamInfo::BytesMeterSharedPtr&& bytes_meter);
  void encodeHeadersBase(const RequestOrResponseHeaderMap& headers, absl::optional<uint64_t> status,
                         bool end_stream, bool bodiless_request);
  void encodeTrailersBase(const HeaderMap& headers);

  Buffer::BufferMemoryAccountSharedPtr buffer_memory_account_;
  ConnectionImpl& connection_;
  uint32_t read_disable_calls_{};
  bool disable_chunk_encoding_ : 1;
  bool chunk_encoding_ : 1;
  bool connect_request_ : 1;
  bool is_tcp_tunneling_ : 1;
  bool is_response_to_head_request_ : 1;
  bool is_response_to_connect_request_ : 1;

private:
  /**
   * Called to encode an individual header.
   * @param key supplies the header to encode as a string_view.
   * @param value supplies the value to encode as a string_view.
   */
  void encodeHeader(absl::string_view key, absl::string_view value);

  /**
   * Called to finalize a stream encode.
   */
  void endEncode();

  /**
   * Encapsulates notification to various objects that the encode completed.
   */
  void notifyEncodeComplete();

  void encodeFormattedHeader(absl::string_view key, absl::string_view value,
                             HeaderKeyFormatterOptConstRef formatter);

  void flushOutput(bool end_encode = false);

  absl::string_view details_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_;
  CodecEventCallbacks* codec_callbacks_{nullptr};
};

/**
 * HTTP/1.1 response encoder.
 */
class ResponseEncoderImpl : public StreamEncoderImpl, public ResponseEncoder {
public:
  ResponseEncoderImpl(ConnectionImpl& connection, StreamInfo::BytesMeterSharedPtr&& bytes_meter,
                      bool stream_error_on_invalid_http_message)
      : StreamEncoderImpl(connection, std::move(bytes_meter)),
        stream_error_on_invalid_http_message_(stream_error_on_invalid_http_message) {}

  ~ResponseEncoderImpl() override {
    // Only the downstream stream should clear the downstream of the
    // memory account.
    //
    // There are cases where a corresponding upstream stream dtor might
    // be called, but the downstream stream isn't going to terminate soon
    // such as StreamDecoderFilterCallbacks::recreateStream().
    if (buffer_memory_account_) {
      buffer_memory_account_->clearDownstream();
    }
  }

  bool startedResponse() { return started_response_; }

  // Http::ResponseEncoder
  void encode1xxHeaders(const ResponseHeaderMap& headers) override;
  void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const ResponseTrailerMap& trailers) override { encodeTrailersBase(trailers); }

  bool streamErrorOnInvalidHttpMessage() const override {
    return stream_error_on_invalid_http_message_;
  }
  void setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr,
                                            Http::ResponseHeaderMapConstSharedPtr,
                                            Http::ResponseTrailerMapConstSharedPtr,
                                            StreamInfo::StreamInfo&) override {}

  // For H/1, ResponseEncoder doesn't hold a pointer to RequestDecoder.
  // TODO(paulsohn): Enable H/1 codec to get a pointer to the new
  // request decoder on recreateStream, here or elsewhere.
  void setRequestDecoder(Http::RequestDecoder& /*decoder*/) override {}

  // Http1::StreamEncoderImpl
  void resetStream(StreamResetReason reason) override;

private:
  bool started_response_{};
  const bool stream_error_on_invalid_http_message_;
};

/**
 * HTTP/1.1 request encoder.
 */
class RequestEncoderImpl : public StreamEncoderImpl, public RequestEncoder {
public:
  RequestEncoderImpl(ConnectionImpl& connection, StreamInfo::BytesMeterSharedPtr&& bytes_meter)
      : StreamEncoderImpl(connection, std::move(bytes_meter)) {}
  bool upgradeRequest() const { return upgrade_request_; }
  bool headRequest() const { return head_request_; }
  bool connectRequest() const { return connect_request_; }

  // Http::RequestEncoder
  Status encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const RequestTrailerMap& trailers) override { encodeTrailersBase(trailers); }
  void enableTcpTunneling() override { is_tcp_tunneling_ = true; }

private:
  bool upgrade_request_{};
  bool head_request_{};
};

/**
 * Base class for HTTP/1.1 client and server connections.
 * Handles the callbacks of http_parser with its own base routine and then
 * virtual dispatches to its subclasses.
 */
class ConnectionImpl : public virtual Connection,
                       protected Logger::Loggable<Logger::Id::http>,
                       public ParserCallbacks,
                       public ScopeTrackedObject {
public:
  /**
   * @return Network::Connection& the backing network connection.
   */
  Network::Connection& connection() { return connection_; }
  const Network::Connection& connection() const { return connection_; }

  /**
   * Called when the active encoder has completed encoding the outbound half of the stream.
   */
  virtual void onEncodeComplete() PURE;

  virtual StreamInfo::BytesMeter& getBytesMeter() PURE;

  /**
   * Called when resetStream() has been called on an active stream. In HTTP/1.1 the only
   * valid operation after this point is for the connection to get blown away, but we will not
   * fire any more callbacks in case some stack has to unwind.
   */
  void onResetStreamBase(StreamResetReason reason);

  /**
   * Flush all pending output from encoding.
   */
  uint64_t flushOutput(bool end_encode = false);

  Buffer::Instance& buffer() { return *output_buffer_; }

  void readDisable(bool disable) {
    if (connection_.state() == Network::Connection::State::Open) {
      connection_.readDisable(disable);
    }
  }
  uint32_t bufferLimit() { return connection_.bufferLimit(); }
  virtual bool supportsHttp10() { return false; }
  bool maybeDirectDispatch(Buffer::Instance& data);
  virtual void maybeAddSentinelBufferFragment(Buffer::Instance&) {}
  CodecStats& stats() { return stats_; }
  bool enableTrailers() const { return codec_settings_.enable_trailers_; }
  virtual bool sendFullyQualifiedUrl() const { return codec_settings_.send_fully_qualified_url_; }
  HeaderKeyFormatterOptConstRef formatter() const {
    return makeOptRefFromPtr(encode_only_header_key_formatter_.get());
  }

  // Http::Connection
  Http::Status dispatch(Buffer::Instance& data) override;
  void goAway() override {} // Called during connection manager drain flow
  Protocol protocol() override { return protocol_; }
  void shutdownNotice() override {} // Called during connection manager drain flow
  bool wantsToWrite() override { return false; }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override { onAboveHighWatermark(); }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override { onBelowLowWatermark(); }

  // Codec errors found in callbacks are overridden within the http_parser library. This holds those
  // errors to propagate them through to dispatch() where we can handle the error.
  Envoy::Http::Status codec_status_;

  // ScopeTrackedObject
  OptRef<const StreamInfo::StreamInfo> trackedStream() const override;
  void dumpState(std::ostream& os, int indent_level) const override;

protected:
  ConnectionImpl(Network::Connection& connection, CodecStats& stats, const Http1Settings& settings,
                 MessageType type, uint32_t max_headers_kb, const uint32_t max_headers_count);

  bool resetStreamCalled() { return reset_stream_called_; }

  // This must be protected because it is called through ServerConnectionImpl::sendProtocolError.
  Status onMessageBeginImpl();

  /**
   * Get memory used to represent HTTP headers or trailers currently being parsed.
   * Computed by adding the partial header field and value that is currently being parsed and the
   * estimated header size for previous header lines provided by HeaderMap::byteSize().
   */
  virtual uint32_t getHeadersSize();

  /**
   * Called from onUrl, onHeaderFields and onHeaderValue to verify that the headers do not exceed
   * the configured max header size limit.
   * @return A codecProtocolError status if headers exceed the size limit.
   */
  Status checkMaxHeadersSize();

  Network::Connection& connection_;
  CodecStats& stats_;
  const Http1Settings codec_settings_;
  std::unique_ptr<Parser> parser_;
  Buffer::Instance* current_dispatching_buffer_{};
  Buffer::Instance* output_buffer_ = nullptr; // Not owned
  Http::Code error_code_{Http::Code::BadRequest};
  const HeaderKeyFormatterConstPtr encode_only_header_key_formatter_;
  HeaderString current_header_field_;
  HeaderString current_header_value_;
  bool processing_trailers_ : 1;
  bool handling_upgrade_ : 1;
  bool reset_stream_called_ : 1;
  // Deferred end stream headers indicate that we are not going to raise headers until the full
  // HTTP/1 message has been flushed from the parser. This allows raising an HTTP/2 style headers
  // block with end stream set to true with no further protocol data remaining.
  bool deferred_end_stream_headers_ : 1;
  bool dispatching_ : 1;
  bool dispatching_slice_already_drained_ : 1;
  StreamInfo::BytesMeterSharedPtr bytes_meter_before_stream_;
  const uint32_t max_headers_kb_;
  const uint32_t max_headers_count_;

private:
  enum class HeaderParsingState { Field, Value, Done };
  friend std::ostream& operator<<(std::ostream& os, HeaderParsingState parsing_state) {
    switch (parsing_state) {
    case ConnectionImpl::HeaderParsingState::Field:
      return os << "Field";
    case ConnectionImpl::HeaderParsingState::Value:
      return os << "Value";
    case ConnectionImpl::HeaderParsingState::Done:
      return os << "Done";
    }
    return os;
  }

  virtual HeaderMap& headersOrTrailers() PURE;
  virtual RequestOrResponseHeaderMap& requestOrResponseHeaders() PURE;
  virtual void allocHeaders(StatefulHeaderKeyFormatterPtr&& formatter) PURE;
  virtual void allocTrailers() PURE;

  /**
   * Called for each header in order to complete an in progress header decode.
   * @return A status representing success.
   */
  Status completeCurrentHeader();

  /**
   * Check if header name contains underscore character.
   * Underscore character is allowed in header names by the RFC-7230 and this check is implemented
   * as a security measure due to systems that treat '_' and '-' as interchangeable.
   * The ServerConnectionImpl may drop header or reject request based on the
   * `common_http_protocol_options.headers_with_underscores_action` configuration option in the
   * HttpConnectionManager.
   */
  virtual bool shouldDropHeaderWithUnderscoresInNames(absl::string_view /* header_name */) const {
    return false;
  }

  /**
   * Dispatch a memory span.
   * @param slice supplies the start address.
   * @len supplies the length of the span.
   */
  Envoy::StatusOr<size_t> dispatchSlice(const char* slice, size_t len);

  void onDispatch(const Buffer::Instance& data);

  // ParserCallbacks.
  CallbackResult onMessageBegin() override;
  CallbackResult onUrl(const char* data, size_t length) override;
  CallbackResult onStatus(const char* data, size_t length) override;
  CallbackResult onHeaderField(const char* data, size_t length) override;
  CallbackResult onHeaderValue(const char* data, size_t length) override;
  CallbackResult onHeadersComplete() override;
  void bufferBody(const char* data, size_t length) override;
  CallbackResult onMessageComplete() override;
  void onChunkHeader(bool is_final_chunk) override;

  // Internal implementations of ParserCallbacks methods,
  // and virtual methods for connection-specific implementations.
  virtual Status onMessageBeginBase() PURE;
  virtual Status onUrlBase(const char* data, size_t length) PURE;
  virtual Status onStatusBase(const char* data, size_t length) PURE;
  Status onHeaderFieldImpl(const char* data, size_t length);
  Status onHeaderValueImpl(const char* data, size_t length);
  StatusOr<CallbackResult> onHeadersCompleteImpl();
  virtual StatusOr<CallbackResult> onHeadersCompleteBase() PURE;
  StatusOr<CallbackResult> onMessageCompleteImpl();
  virtual CallbackResult onMessageCompleteBase() PURE;

  // These helpers wrap *Impl() calls in the overrides of non-void
  // ParserCallbacks methods.
  CallbackResult setAndCheckCallbackStatus(Status&& status);
  CallbackResult setAndCheckCallbackStatusOr(StatusOr<CallbackResult>&& statusor);

  /**
   * Push the accumulated body through the filter pipeline.
   */
  void dispatchBufferedBody();

  /**
   * Called to see if upgrade transition is allowed.
   */
  virtual bool upgradeAllowed() const PURE;

  /**
   * Called with body data is available for processing when either:
   * - There is an accumulated partial body after the parser is done processing bytes read from the
   * socket
   * - The parser encounters the last byte of the body
   * - The codec does a direct dispatch from the read buffer
   * For performance reasons there is at most one call to onBody per call to HTTP/1
   * ConnectionImpl::dispatch call.
   * @param data supplies the body data
   */
  virtual void onBody(Buffer::Instance& data) PURE;

  /**
   * @see onResetStreamBase().
   */
  virtual void onResetStream(StreamResetReason reason) PURE;

  /**
   * Send a protocol error response to remote.
   */
  virtual Status sendProtocolError(absl::string_view details) PURE;

  /**
   * Called when output_buffer_ or the underlying connection go from below a low watermark to over
   * a high watermark.
   */
  virtual void onAboveHighWatermark() PURE;

  /**
   * Called when output_buffer_ or the underlying connection  go from above a high watermark to
   * below a low watermark.
   */
  virtual void onBelowLowWatermark() PURE;

  /**
   * Check if header name contains underscore character.
   * The ServerConnectionImpl may drop header or reject request based on configuration.
   * @return A status representing whether the request is rejected.
   */
  virtual Status checkHeaderNameForUnderscores() { return okStatus(); }

  /**
   * Additional state to dump on crash.
   */
  virtual void dumpAdditionalState(std::ostream& os, int indent_level) const PURE;

  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};
  // Used to accumulate the HTTP message body during the current dispatch call. The accumulated body
  // is pushed through the filter pipeline either at the end of the current dispatch call, or when
  // the last byte of the body is processed (whichever happens first).
  Buffer::OwnedImpl buffered_body_;
  Protocol protocol_{Protocol::Http11};
};

/**
 * Implementation of Http::ServerConnection for HTTP/1.1.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, CodecStats& stats,
                       ServerConnectionCallbacks& callbacks, const Http1Settings& settings,
                       uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
                       envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                           headers_with_underscores_action,
                       Server::OverloadManager& overload_manager);
  bool supportsHttp10() override { return codec_settings_.accept_http_10_; }

protected:
  /**
   * An active HTTP/1.1 request.
   */
  struct ActiveRequest : public Event::DeferredDeletable {
    ActiveRequest(ServerConnectionImpl& connection, StreamInfo::BytesMeterSharedPtr&& bytes_meter)
        : response_encoder_(connection, std::move(bytes_meter),
                            connection.codec_settings_.stream_error_on_invalid_http_message_) {}
    ~ActiveRequest() override = default;

    void dumpState(std::ostream& os, int indent_level) const;
    HeaderString request_url_;
    RequestDecoder* request_decoder_{};
    ResponseEncoderImpl response_encoder_;
    bool remote_complete_{};
  };
  ActiveRequest* activeRequest() { return active_request_.get(); }
  // ConnectionImpl
  CallbackResult onMessageCompleteBase() override;
  // Add the size of the request_url to the reported header size when processing request headers.
  uint32_t getHeadersSize() override;

private:
  /**
   * Manipulate the request's first line, parsing the url and converting to a relative path if
   * necessary. Compute Host / :authority headers based on 7230#5.7 and 7230#6
   *
   * @param is_connect true if the request has the CONNECT method
   * @param headers the request's headers
   * @return Status representing success or failure. This will fail if there is an invalid url in
   * the request line.
   */
  Status handlePath(RequestHeaderMap& headers, absl::string_view method);

  // ParserCallbacks.
  Status onUrlBase(const char* data, size_t length) override;
  Status onStatusBase(const char*, size_t) override { return okStatus(); }
  // ConnectionImpl
  Http::Status dispatch(Buffer::Instance& data) override;
  void onEncodeComplete() override;
  StreamInfo::BytesMeter& getBytesMeter() override {
    if (active_request_) {
      return *(active_request_->response_encoder_.getStream().bytesMeter());
    }
    if (bytes_meter_before_stream_ == nullptr) {
      bytes_meter_before_stream_ = std::make_shared<StreamInfo::BytesMeter>();
    }
    return *bytes_meter_before_stream_;
  }
  Status onMessageBeginBase() override;
  Envoy::StatusOr<CallbackResult> onHeadersCompleteBase() override;
  // If upgrade behavior is not allowed, the HCM will have sanitized the headers out.
  bool upgradeAllowed() const override { return true; }
  void onBody(Buffer::Instance& data) override;
  void onResetStream(StreamResetReason reason) override;
  Status sendProtocolError(absl::string_view details) override;
  Status sendOverloadError();
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;
  HeaderMap& headersOrTrailers() override {
    if (absl::holds_alternative<RequestHeaderMapPtr>(headers_or_trailers_)) {
      return *absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
    } else {
      return *absl::get<RequestTrailerMapPtr>(headers_or_trailers_);
    }
  }
  RequestOrResponseHeaderMap& requestOrResponseHeaders() override {
    return *absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
  }
  void allocHeaders(StatefulHeaderKeyFormatterPtr&& formatter) override {
    ASSERT(nullptr == absl::get<RequestHeaderMapPtr>(headers_or_trailers_));
    ASSERT(!processing_trailers_);
    auto headers = RequestHeaderMapImpl::create(max_headers_kb_, max_headers_count_);
    headers->setFormatter(std::move(formatter));
    headers_or_trailers_.emplace<RequestHeaderMapPtr>(std::move(headers));
  }
  void allocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<RequestTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<RequestTrailerMapPtr>(
          RequestTrailerMapImpl::create(max_headers_kb_, max_headers_count_));
    }
  }
  void dumpAdditionalState(std::ostream& os, int indent_level) const override;

  void releaseOutboundResponse(const Buffer::OwnedBufferFragmentImpl* fragment);
  void maybeAddSentinelBufferFragment(Buffer::Instance& output_buffer) override;

  Status doFloodProtectionChecks() const;
  Status checkHeaderNameForUnderscores() override;
  Status checkProtocolVersion(RequestHeaderMap& headers);

  ServerConnectionCallbacks& callbacks_;
  std::unique_ptr<ActiveRequest> active_request_;
  const Buffer::OwnedBufferFragmentImpl::Releasor response_buffer_releasor_;
  uint32_t outbound_responses_{};
  // Buffer used to encode the HTTP message before moving it to the network connection's output
  // buffer. This buffer is always allocated, never nullptr.
  Buffer::InstancePtr owned_output_buffer_;
  // TODO(mattklein123): This should be a member of ActiveRequest but this change needs dedicated
  // thought as some of the reset and no header code paths make this difficult. Headers are
  // populated on message begin. Trailers are populated on the first parsed trailer field (if
  // trailers are enabled). The variant is reset to null headers on message complete for assertion
  // purposes.
  absl::variant<RequestHeaderMapPtr, RequestTrailerMapPtr> headers_or_trailers_;
  // The action to take when a request header name contains underscore characters.
  const envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
  Server::LoadShedPoint* abort_dispatch_{nullptr};
};

/**
 * Implementation of Http::ClientConnection for HTTP/1.1.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, CodecStats& stats,
                       ConnectionCallbacks& callbacks, const Http1Settings& settings,
                       absl::optional<uint16_t> max_response_headers_kb,
                       const uint32_t max_response_headers_count,
                       bool passing_through_proxy = false);
  // Http::ClientConnection
  RequestEncoder& newStream(ResponseDecoder& response_decoder) override;

private:
  struct PendingResponse {
    PendingResponse(ConnectionImpl& connection, StreamInfo::BytesMeterSharedPtr&& bytes_meter,
                    ResponseDecoder* decoder)
        : encoder_(connection, std::move(bytes_meter)), decoder_(decoder) {}
    RequestEncoderImpl encoder_;
    ResponseDecoder* decoder_;
  };

  bool cannotHaveBody();

  bool sendFullyQualifiedUrl() const override {
    // Send fully qualified URLs either if the parent connection is configured to do so or this
    // stream is passing through a proxy and the underlying transport is plaintext.
    return ConnectionImpl::sendFullyQualifiedUrl() ||
           (passing_through_proxy_ && !connection().ssl());
  }

  // ParserCallbacks.
  Status onUrlBase(const char*, size_t) override { return okStatus(); }
  Status onStatusBase(const char* data, size_t length) override;
  // ConnectionImpl
  Http::Status dispatch(Buffer::Instance& data) override;
  void onEncodeComplete() override { encode_complete_ = true; }
  StreamInfo::BytesMeter& getBytesMeter() override {
    if (pending_response_.has_value()) {
      return *(pending_response_->encoder_.getStream().bytesMeter());
    }
    if (bytes_meter_before_stream_ == nullptr) {
      bytes_meter_before_stream_ = std::make_shared<StreamInfo::BytesMeter>();
    }
    return *bytes_meter_before_stream_;
  }
  Status onMessageBeginBase() override { return okStatus(); }
  Envoy::StatusOr<CallbackResult> onHeadersCompleteBase() override;
  bool upgradeAllowed() const override;
  void onBody(Buffer::Instance& data) override;
  CallbackResult onMessageCompleteBase() override;
  void onResetStream(StreamResetReason reason) override;
  Status sendProtocolError(absl::string_view details) override;
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;
  HeaderMap& headersOrTrailers() override {
    if (absl::holds_alternative<ResponseHeaderMapPtr>(headers_or_trailers_)) {
      return *absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
    } else {
      return *absl::get<ResponseTrailerMapPtr>(headers_or_trailers_);
    }
  }
  RequestOrResponseHeaderMap& requestOrResponseHeaders() override {
    return *absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
  }
  void allocHeaders(StatefulHeaderKeyFormatterPtr&& formatter) override {
    ASSERT(nullptr == absl::get<ResponseHeaderMapPtr>(headers_or_trailers_));
    ASSERT(!processing_trailers_);
    auto headers = ResponseHeaderMapImpl::create(max_headers_kb_, max_headers_count_);
    headers->setFormatter(std::move(formatter));
    headers_or_trailers_.emplace<ResponseHeaderMapPtr>(std::move(headers));
  }
  void allocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<ResponseTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<ResponseTrailerMapPtr>(
          ResponseTrailerMapImpl::create(max_headers_kb_, max_headers_count_));
    }
  }
  void dumpAdditionalState(std::ostream& os, int indent_level) const override;

  // Buffer used to encode the HTTP message before moving it to the network connection's output
  // buffer. This buffer is always allocated, never nullptr.
  Buffer::InstancePtr owned_output_buffer_;

  absl::optional<PendingResponse> pending_response_;
  // TODO(mattklein123): The following bool tracks whether a pending response is complete before
  // dispatching callbacks. This is needed so that pending_response_ stays valid during callbacks
  // in order to access the stream, but to avoid invoking callbacks that shouldn't be called once
  // the response is complete. The existence of this variable is hard to reason about and it should
  // be combined with pending_response_ somehow in a follow up cleanup.
  bool pending_response_done_{true};
  // Set true between receiving non-101 1xx headers and receiving the spurious onMessageComplete.
  bool ignore_message_complete_for_1xx_{};
  // TODO(mattklein123): This should be a member of PendingResponse but this change needs dedicated
  // thought as some of the reset and no header code paths make this difficult. Headers are
  // populated on message begin. Trailers are populated when the switch to trailer processing is
  // detected while parsing the first trailer field (if trailers are enabled). The variant is reset
  // to null headers on message complete for assertion purposes.
  absl::variant<ResponseHeaderMapPtr, ResponseTrailerMapPtr> headers_or_trailers_;

  // True if the upstream connection is pointed at an HTTP/1.1 proxy, and
  // plaintext HTTP should be sent with fully qualified URLs.
  bool passing_through_proxy_ = false;

  const bool force_reset_on_premature_upstream_half_close_{};
  bool encode_complete_{false};
};

} // namespace Http1
} // namespace Http
} // namespace Envoy

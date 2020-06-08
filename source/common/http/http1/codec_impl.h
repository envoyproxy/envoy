#pragma once

#include <http_parser.h>

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/assert.h"
#include "common/common/statusor.h"
#include "common/http/codec_helper.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_stats.h"
#include "common/http/http1/header_formatter.h"
#include "common/http/status.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class ConnectionImpl;

/**
 * Base class for HTTP/1.1 request and response encoders.
 */
class StreamEncoderImpl : public virtual StreamEncoder,
                          public Stream,
                          Logger::Loggable<Logger::Id::http>,
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
  // After this is called, for the HTTP/1 codec, the connection should be closed, i.e. no further
  // progress may be made with the codec.
  void resetStream(StreamResetReason reason) override;
  void readDisable(bool disable) override;
  uint32_t bufferLimit() override;
  absl::string_view responseDetails() override { return details_; }
  const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override;
  void setFlushTimeout(std::chrono::milliseconds) override {
    // HTTP/1 has one stream per connection, thus any data encoded is immediately written to the
    // connection, invoking any watermarks as necessary. There is no internal buffering that would
    // require a flush timeout not already covered by other timeouts.
  }

  void setIsResponseToHeadRequest(bool value) { is_response_to_head_request_ = value; }
  void setIsResponseToConnectRequest(bool value) { is_response_to_connect_request_ = value; }
  void setDetails(absl::string_view details) { details_ = details; }

  void clearReadDisableCallsForTests() { read_disable_calls_ = 0; }

protected:
  StreamEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter);
  void encodeHeadersBase(const RequestOrResponseHeaderMap& headers, absl::optional<uint64_t> status,
                         bool end_stream);
  void encodeTrailersBase(const HeaderMap& headers);

  static const std::string CRLF;
  static const std::string LAST_CHUNK;

  ConnectionImpl& connection_;
  uint32_t read_disable_calls_{};
  bool disable_chunk_encoding_ : 1;
  bool chunk_encoding_ : 1;
  bool is_response_to_head_request_ : 1;
  bool is_response_to_connect_request_ : 1;

private:
  /**
   * Called to encode an individual header.
   * @param key supplies the header to encode.
   * @param key_size supplies the byte size of the key.
   * @param value supplies the value to encode.
   * @param value_size supplies the byte size of the value.
   */
  void encodeHeader(const char* key, uint32_t key_size, const char* value, uint32_t value_size);

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

  void encodeFormattedHeader(absl::string_view key, absl::string_view value);

  const HeaderKeyFormatter* const header_key_formatter_;
  absl::string_view details_;
};

/**
 * HTTP/1.1 response encoder.
 */
class ResponseEncoderImpl : public StreamEncoderImpl, public ResponseEncoder {
public:
  ResponseEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter)
      : StreamEncoderImpl(connection, header_key_formatter) {}

  bool startedResponse() { return started_response_; }

  // Http::ResponseEncoder
  void encode100ContinueHeaders(const ResponseHeaderMap& headers) override;
  void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const ResponseTrailerMap& trailers) override { encodeTrailersBase(trailers); }

private:
  bool started_response_{};
};

/**
 * HTTP/1.1 request encoder.
 */
class RequestEncoderImpl : public StreamEncoderImpl, public RequestEncoder {
public:
  RequestEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter)
      : StreamEncoderImpl(connection, header_key_formatter) {}
  bool upgradeRequest() const { return upgrade_request_; }
  bool headRequest() const { return head_request_; }
  bool connectRequest() const { return connect_request_; }

  // Http::RequestEncoder
  void encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const RequestTrailerMap& trailers) override { encodeTrailersBase(trailers); }

private:
  bool upgrade_request_{};
  bool head_request_{};
  bool connect_request_{};
};

/**
 * Base class for HTTP/1.1 client and server connections.
 * Handles the callbacks of http_parser with its own base routine and then
 * virtual dispatches to its subclasses.
 */
class ConnectionImpl : public virtual Connection, protected Logger::Loggable<Logger::Id::http> {
public:
  /**
   * @return Network::Connection& the backing network connection.
   */
  Network::Connection& connection() { return connection_; }

  /**
   * Called when the active encoder has completed encoding the outbound half of the stream.
   */
  virtual void onEncodeComplete() PURE;

  /**
   * Called when resetStream() has been called on an active stream. In HTTP/1.1 the only
   * valid operation after this point is for the connection to get blown away, but we will not
   * fire any more callbacks in case some stack has to unwind.
   */
  void onResetStreamBase(StreamResetReason reason);

  /**
   * Flush all pending output from encoding.
   */
  void flushOutput(bool end_encode = false);

  void addToBuffer(absl::string_view data);
  void addCharToBuffer(char c);
  void addIntToBuffer(uint64_t i);
  Buffer::WatermarkBuffer& buffer() { return output_buffer_; }
  uint64_t bufferRemainingSize();
  void copyToBuffer(const char* data, uint64_t length);
  void reserveBuffer(uint64_t size);
  void readDisable(bool disable) {
    if (connection_.state() == Network::Connection::State::Open) {
      connection_.readDisable(disable);
    }
  }
  uint32_t bufferLimit() { return connection_.bufferLimit(); }
  virtual bool supportsHttp10() { return false; }
  bool maybeDirectDispatch(Buffer::Instance& data);
  virtual void maybeAddSentinelBufferFragment(Buffer::WatermarkBuffer&) {}
  CodecStats& stats() { return stats_; }
  bool enableTrailers() const { return enable_trailers_; }

  // Http::Connection
  Http::Status dispatch(Buffer::Instance& data) override;
  void goAway() override {} // Called during connection manager drain flow
  Protocol protocol() override { return protocol_; }
  void shutdownNotice() override {} // Called during connection manager drain flow
  bool wantsToWrite() override { return false; }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override { onAboveHighWatermark(); }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override { onBelowLowWatermark(); }

  bool strict1xxAnd204Headers() { return strict_1xx_and_204_headers_; }

protected:
  ConnectionImpl(Network::Connection& connection, CodecStats& stats, http_parser_type type,
                 uint32_t max_headers_kb, const uint32_t max_headers_count,
                 HeaderKeyFormatterPtr&& header_key_formatter, bool enable_trailers);

  bool resetStreamCalled() { return reset_stream_called_; }
  void onMessageBeginBase();

  /**
   * Get memory used to represent HTTP headers or trailers currently being parsed.
   * Computed by adding the partial header field and value that is currently being parsed and the
   * estimated header size for previous header lines provided by HeaderMap::byteSize().
   */
  virtual uint32_t getHeadersSize();

  /**
   * Called from onUrl, onHeaderField and onHeaderValue to verify that the headers do not exceed the
   * configured max header size limit. Throws a  CodecProtocolException if headers exceed the size
   * limit.
   */
  void checkMaxHeadersSize();

  Network::Connection& connection_;
  CodecStats& stats_;
  http_parser parser_;
  Http::Code error_code_{Http::Code::BadRequest};
  const HeaderKeyFormatterPtr header_key_formatter_;
  HeaderString current_header_field_;
  HeaderString current_header_value_;
  bool processing_trailers_ : 1;
  bool handling_upgrade_ : 1;
  bool reset_stream_called_ : 1;
  // Deferred end stream headers indicate that we are not going to raise headers until the full
  // HTTP/1 message has been flushed from the parser. This allows raising an HTTP/2 style headers
  // block with end stream set to true with no further protocol data remaining.
  bool deferred_end_stream_headers_ : 1;
  const bool strict_header_validation_ : 1;
  const bool connection_header_sanitization_ : 1;
  const bool enable_trailers_ : 1;
  const bool reject_unsupported_transfer_encodings_ : 1;
  const bool strict_1xx_and_204_headers_ : 1;

private:
  enum class HeaderParsingState { Field, Value, Done };

  virtual HeaderMap& headersOrTrailers() PURE;
  virtual RequestOrResponseHeaderMap& requestOrResponseHeaders() PURE;
  virtual void allocHeaders() PURE;
  virtual void allocTrailers() PURE;

  /**
   * Called in order to complete an in progress header decode.
   */
  void completeLastHeader();

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
   * An inner dispatch call that executes the dispatching logic. While exception removal is in
   * migration (#10878), this function may either throw an exception or return an error status.
   * Exceptions are caught and translated to their corresponding statuses in the outer level
   * dispatch.
   * TODO(#10878): Remove this when exception removal is complete.
   */
  Http::Status innerDispatch(Buffer::Instance& data);

  /**
   * Dispatch a memory span.
   * @param slice supplies the start address.
   * @len supplies the length of the span.
   */
  size_t dispatchSlice(const char* slice, size_t len);

  /**
   * Called by the http_parser when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  void bufferBody(const char* data, size_t length);

  /**
   * Push the accumulated body through the filter pipeline.
   */
  void dispatchBufferedBody();

  /**
   * Called when a request/response is beginning. A base routine happens first then a virtual
   * dispatch is invoked.
   */
  virtual void onMessageBegin() PURE;

  /**
   * Called when URL data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  virtual void onUrl(const char* data, size_t length) PURE;

  /**
   * Called when header field data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  void onHeaderField(const char* data, size_t length);

  /**
   * Called when header value data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  void onHeaderValue(const char* data, size_t length);

  /**
   * Called when headers are complete. A base routine happens first then a virtual dispatch is
   * invoked. Note that this only applies to headers and NOT trailers. End of
   * trailers are signaled via onMessageCompleteBase().
   * @return 0 if no error, 1 if there should be no body.
   */
  int onHeadersCompleteBase();
  virtual int onHeadersComplete() PURE;

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
   * Called when the request/response is complete.
   */
  void onMessageCompleteBase();
  virtual void onMessageComplete() PURE;

  /**
   * Called when accepting a chunk header.
   */
  void onChunkHeader(bool is_final_chunk);

  /**
   * @see onResetStreamBase().
   */
  virtual void onResetStream(StreamResetReason reason) PURE;

  /**
   * Send a protocol error response to remote.
   */
  virtual void sendProtocolError(absl::string_view details) PURE;

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
   */
  virtual void checkHeaderNameForUnderscores() {}

  static http_parser_settings settings_;

  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};
  // Used to accumulate the HTTP message body during the current dispatch call. The accumulated body
  // is pushed through the filter pipeline either at the end of the current dispatch call, or when
  // the last byte of the body is processed (whichever happens first).
  Buffer::OwnedImpl buffered_body_;
  Buffer::WatermarkBuffer output_buffer_;
  Protocol protocol_{Protocol::Http11};
  const uint32_t max_headers_kb_;
  const uint32_t max_headers_count_;
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
                           headers_with_underscores_action);
  bool supportsHttp10() override { return codec_settings_.accept_http_10_; }

protected:
  /**
   * An active HTTP/1.1 request.
   */
  struct ActiveRequest {
    ActiveRequest(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter)
        : response_encoder_(connection, header_key_formatter) {}

    HeaderString request_url_;
    RequestDecoder* request_decoder_{};
    ResponseEncoderImpl response_encoder_;
    bool remote_complete_{};
  };
  absl::optional<ActiveRequest>& activeRequest() { return active_request_; }
  // ConnectionImpl
  void onMessageComplete() override;
  // Add the size of the request_url to the reported header size when processing request headers.
  uint32_t getHeadersSize() override;

private:
  /**
   * Manipulate the request's first line, parsing the url and converting to a relative path if
   * necessary. Compute Host / :authority headers based on 7230#5.7 and 7230#6
   *
   * @param is_connect true if the request has the CONNECT method
   * @param headers the request's headers
   * @throws CodecProtocolException on an invalid url in the request line
   */
  void handlePath(RequestHeaderMap& headers, unsigned int method);

  // ConnectionImpl
  void onEncodeComplete() override;
  void onMessageBegin() override;
  void onUrl(const char* data, size_t length) override;
  int onHeadersComplete() override;
  // If upgrade behavior is not allowed, the HCM will have sanitized the headers out.
  bool upgradeAllowed() const override { return true; }
  void onBody(Buffer::Instance& data) override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError(absl::string_view details) override;
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
  void allocHeaders() override {
    ASSERT(nullptr == absl::get<RequestHeaderMapPtr>(headers_or_trailers_));
    ASSERT(!processing_trailers_);
    headers_or_trailers_.emplace<RequestHeaderMapPtr>(RequestHeaderMapImpl::create());
  }
  void allocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<RequestTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<RequestTrailerMapPtr>(RequestTrailerMapImpl::create());
    }
  }

  void sendProtocolErrorOld(absl::string_view details);

  void releaseOutboundResponse(const Buffer::OwnedBufferFragmentImpl* fragment);
  void maybeAddSentinelBufferFragment(Buffer::WatermarkBuffer& output_buffer) override;
  void doFloodProtectionChecks() const;
  void checkHeaderNameForUnderscores() override;

  ServerConnectionCallbacks& callbacks_;
  absl::optional<ActiveRequest> active_request_;
  Http1Settings codec_settings_;
  const Buffer::OwnedBufferFragmentImpl::Releasor response_buffer_releasor_;
  uint32_t outbound_responses_{};
  // This defaults to 2, which functionally disables pipelining. If any users
  // of Envoy wish to enable pipelining (which is dangerous and ill supported)
  // we could make this configurable.
  uint32_t max_outbound_responses_{};
  bool flood_protection_{};
  // TODO(mattklein123): This should be a member of ActiveRequest but this change needs dedicated
  // thought as some of the reset and no header code paths make this difficult. Headers are
  // populated on message begin. Trailers are populated on the first parsed trailer field (if
  // trailers are enabled). The variant is reset to null headers on message complete for assertion
  // purposes.
  absl::variant<RequestHeaderMapPtr, RequestTrailerMapPtr> headers_or_trailers_;
  // The action to take when a request header name contains underscore characters.
  const envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
};

/**
 * Implementation of Http::ClientConnection for HTTP/1.1.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, CodecStats& stats,
                       ConnectionCallbacks& callbacks, const Http1Settings& settings,
                       const uint32_t max_response_headers_count);

  // Http::ClientConnection
  RequestEncoder& newStream(ResponseDecoder& response_decoder) override;

private:
  struct PendingResponse {
    PendingResponse(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter,
                    ResponseDecoder* decoder)
        : encoder_(connection, header_key_formatter), decoder_(decoder) {}

    RequestEncoderImpl encoder_;
    ResponseDecoder* decoder_;
  };

  bool cannotHaveBody();

  // ConnectionImpl
  void onEncodeComplete() override {}
  void onMessageBegin() override {}
  void onUrl(const char*, size_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  int onHeadersComplete() override;
  bool upgradeAllowed() const override;
  void onBody(Buffer::Instance& data) override;
  void onMessageComplete() override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError(absl::string_view details) override;
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
  void allocHeaders() override {
    ASSERT(nullptr == absl::get<ResponseHeaderMapPtr>(headers_or_trailers_));
    ASSERT(!processing_trailers_);
    headers_or_trailers_.emplace<ResponseHeaderMapPtr>(ResponseHeaderMapImpl::create());
  }
  void allocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<ResponseTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<ResponseTrailerMapPtr>(ResponseTrailerMapImpl::create());
    }
  }

  absl::optional<PendingResponse> pending_response_;
  // TODO(mattklein123): The following bool tracks whether a pending response is complete before
  // dispatching callbacks. This is needed so that pending_response_ stays valid during callbacks
  // in order to access the stream, but to avoid invoking callbacks that shouldn't be called once
  // the response is complete. The existence of this variable is hard to reason about and it should
  // be combined with pending_response_ somehow in a follow up cleanup.
  bool pending_response_done_{true};
  // Set true between receiving 100-Continue headers and receiving the spurious onMessageComplete.
  bool ignore_message_complete_for_100_continue_{};
  // TODO(mattklein123): This should be a member of PendingResponse but this change needs dedicated
  // thought as some of the reset and no header code paths make this difficult. Headers are
  // populated on message begin. Trailers are populated when the switch to trailer processing is
  // detected while parsing the first trailer field (if trailers are enabled). The variant is reset
  // to null headers on message complete for assertion purposes.
  absl::variant<ResponseHeaderMapPtr, ResponseTrailerMapPtr> headers_or_trailers_;

  // The default limit of 80 KiB is the vanilla http_parser behaviour.
  static constexpr uint32_t MAX_RESPONSE_HEADERS_KB = 80;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy

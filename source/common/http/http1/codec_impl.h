#pragma once

#include <http_parser.h>

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/assert.h"
#include "common/http/codec_helper.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/header_formatter.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * All stats for the HTTP/1 codec. @see stats_macros.h
 */
#define ALL_HTTP1_CODEC_STATS(COUNTER)                                                             \
  COUNTER(metadata_not_supported_error)                                                            \
  COUNTER(response_flood)

/**
 * Wrapper struct for the HTTP/1 codec stats. @see stats_macros.h
 */
struct CodecStats {
  ALL_HTTP1_CODEC_STATS(GENERATE_COUNTER_STRUCT)
};

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
  // Http::StreamEncoder
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const MetadataMapVector&) override;
  Stream& getStream() override { return *this; }
  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return *this; }

  // Http::Http1StreamEncoderOptions
  void disableChunkEncoding() override { disable_chunk_encoding_ = true; }

  // Http::Stream
  void addCallbacks(StreamCallbacks& callbacks) override { addCallbacks_(callbacks); }
  void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  void resetStream(StreamResetReason reason) override;
  void readDisable(bool disable) override;
  uint32_t bufferLimit() override;
  absl::string_view responseDetails() override { return details_; }
  const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override;

  void isResponseToHeadRequest(bool value) { is_response_to_head_request_ = value; }
  void setDetails(absl::string_view details) { details_ = details; }

protected:
  StreamEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter);
  void setIsContentLengthAllowed(bool value) { is_content_length_allowed_ = value; }
  void encodeHeadersBase(const RequestOrResponseHeaderMap& headers, bool end_stream);
  void encodeTrailersBase(const HeaderMap& headers);

  static const std::string CRLF;
  static const std::string LAST_CHUNK;

  ConnectionImpl& connection_;
  bool disable_chunk_encoding_ : 1;
  bool chunk_encoding_ : 1;
  bool processing_100_continue_ : 1;
  bool is_response_to_head_request_ : 1;
  bool is_content_length_allowed_ : 1;

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
  using FloodChecks = std::function<void()>;

  ResponseEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter,
                      FloodChecks& flood_checks)
      : StreamEncoderImpl(connection, header_key_formatter), flood_checks_(flood_checks) {}

  bool startedResponse() { return started_response_; }

  // Http::ResponseEncoder
  void encode100ContinueHeaders(const ResponseHeaderMap& headers) override;
  void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const ResponseTrailerMap& trailers) override { encodeTrailersBase(trailers); }

private:
  FloodChecks& flood_checks_;
  bool started_response_{};
};

/**
 * HTTP/1.1 request encoder.
 */
class RequestEncoderImpl : public StreamEncoderImpl, public RequestEncoder {
public:
  RequestEncoderImpl(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter)
      : StreamEncoderImpl(connection, header_key_formatter) {}
  bool headRequest() { return head_request_; }

  // Http::RequestEncoder
  void encodeHeaders(const RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const RequestTrailerMap& trailers) override { encodeTrailersBase(trailers); }

private:
  bool head_request_{};
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
  void readDisable(bool disable) { connection_.readDisable(disable); }
  uint32_t bufferLimit() { return connection_.bufferLimit(); }
  virtual bool supports_http_10() { return false; }
  bool maybeDirectDispatch(Buffer::Instance& data);
  virtual void maybeAddSentinelBufferFragment(Buffer::WatermarkBuffer&) {}
  CodecStats& stats() { return stats_; }
  bool enableTrailers() const { return enable_trailers_; }

  // Http::Connection
  void dispatch(Buffer::Instance& data) override;
  void goAway() override {} // Called during connection manager drain flow
  Protocol protocol() override { return protocol_; }
  void shutdownNotice() override {} // Called during connection manager drain flow
  bool wantsToWrite() override { return false; }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override { onAboveHighWatermark(); }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override { onBelowLowWatermark(); }

protected:
  ConnectionImpl(Network::Connection& connection, Stats::Scope& stats, http_parser_type type,
                 uint32_t max_headers_kb, const uint32_t max_headers_count,
                 HeaderKeyFormatterPtr&& header_key_formatter, bool enable_trailers);

  bool resetStreamCalled() { return reset_stream_called_; }

  Network::Connection& connection_;
  CodecStats stats_;
  http_parser parser_;
  Http::Code error_code_{Http::Code::BadRequest};
  const HeaderKeyFormatterPtr header_key_formatter_;
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

private:
  enum class HeaderParsingState { Field, Value, Done };

  virtual HeaderMap& headersOrTrailers() PURE;
  virtual RequestOrResponseHeaderMap& requestOrResponseHeaders() PURE;
  virtual void allocHeaders() PURE;
  virtual void maybeAllocTrailers() PURE;

  /**
   * Called in order to complete an in progress header decode.
   */
  void completeLastHeader();

  /**
   * Dispatch a memory span.
   * @param slice supplies the start address.
   * @len supplies the length of the span.
   */
  size_t dispatchSlice(const char* slice, size_t len);

  /**
   * Called when a request/response is beginning. A base routine happens first then a virtual
   * dispatch is invoked.
   */
  void onMessageBeginBase();
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
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  virtual void onBody(const char* data, size_t length) PURE;

  /**
   * Called when the request/response is complete.
   */
  void onMessageCompleteBase();
  virtual void onMessageComplete() PURE;

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

  static http_parser_settings settings_;

  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};
  HeaderString current_header_field_;
  HeaderString current_header_value_;
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
  using FloodChecks = std::function<void()>;
  ServerConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                       ServerConnectionCallbacks& callbacks, const Http1Settings& settings,
                       uint32_t max_request_headers_kb, const uint32_t max_request_headers_count);

  bool supports_http_10() override { return codec_settings_.accept_http_10_; }

private:
  /**
   * An active HTTP/1.1 request.
   */
  struct ActiveRequest {
    ActiveRequest(ConnectionImpl& connection, HeaderKeyFormatter* header_key_formatter,
                  FloodChecks& flood_checks)
        : response_encoder_(connection, header_key_formatter, flood_checks) {}

    HeaderString request_url_;
    RequestDecoder* request_decoder_{};
    ResponseEncoderImpl response_encoder_;
    bool remote_complete_{};
  };

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
  void onBody(const char* data, size_t length) override;
  void onMessageComplete() override;
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
    headers_or_trailers_.emplace<RequestHeaderMapPtr>(std::make_unique<RequestHeaderMapImpl>());
  }
  void maybeAllocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<RequestTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<RequestTrailerMapPtr>(std::make_unique<RequestTrailerMapImpl>());
    }
  }

  void releaseOutboundResponse(const Buffer::OwnedBufferFragmentImpl* fragment);
  void maybeAddSentinelBufferFragment(Buffer::WatermarkBuffer& output_buffer) override;
  void doFloodProtectionChecks() const;

  ServerConnectionCallbacks& callbacks_;
  std::function<void()> flood_checks_{[&]() { this->doFloodProtectionChecks(); }};
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
};

/**
 * Implementation of Http::ClientConnection for HTTP/1.1.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
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
  void onBody(const char* data, size_t length) override;
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
    headers_or_trailers_.emplace<ResponseHeaderMapPtr>(std::make_unique<ResponseHeaderMapImpl>());
  }
  void maybeAllocTrailers() override {
    ASSERT(processing_trailers_);
    if (!absl::holds_alternative<ResponseTrailerMapPtr>(headers_or_trailers_)) {
      headers_or_trailers_.emplace<ResponseTrailerMapPtr>(
          std::make_unique<ResponseTrailerMapImpl>());
    }
  }

  absl::optional<PendingResponse> pending_response_;
  // Set true between receiving 100-Continue headers and receiving the spurious onMessageComplete.
  bool ignore_message_complete_for_100_continue_{};
  // TODO(mattklein123): This should be a member of PendingResponse but this change needs dedicated
  // thought as some of the reset and no header code paths make this difficult. Headers are
  // populated on message begin. Trailers are populated on the first parsed trailer field (if
  // trailers are enabled). The variant is reset to null headers on message complete for assertion
  // purposes.
  absl::variant<ResponseHeaderMapPtr, ResponseTrailerMapPtr> headers_or_trailers_;

  // The default limit of 80 KiB is the vanilla http_parser behaviour.
  static constexpr uint32_t MAX_RESPONSE_HEADERS_KB = 80;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy

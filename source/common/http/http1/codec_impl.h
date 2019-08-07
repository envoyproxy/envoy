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
#include "common/common/to_lower_table.h"
#include "common/http/codec_helper.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * All stats for the HTTP/1 codec. @see stats_macros.h
 */
// clang-format off
#define ALL_HTTP1_CODEC_STATS(COUNTER)                                                             \
  COUNTER(metadata_not_supported_error)                                                            \
// clang-format on

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
class StreamEncoderImpl : public StreamEncoder,
                          public Stream,
                          Logger::Loggable<Logger::Id::http>,
                          public StreamCallbackHelper {
public:
  // Http::StreamEncoder
  void encode100ContinueHeaders(const HeaderMap& headers) override;
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(const HeaderMap& trailers) override;
  void encodeMetadata(const MetadataMapVector&) override;
  Stream& getStream() override { return *this; }

  // Http::Stream
  void addCallbacks(StreamCallbacks& callbacks) override { addCallbacks_(callbacks); }
  void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  void resetStream(StreamResetReason reason) override;
  void readDisable(bool disable) override;
  uint32_t bufferLimit() override;

  void isResponseToHeadRequest(bool value) { is_response_to_head_request_ = value; }

protected:
  StreamEncoderImpl(ConnectionImpl& connection);

  static const std::string CRLF;
  static const std::string LAST_CHUNK;

  ConnectionImpl& connection_;
  void setIsContentLengthAllowed(bool value) { is_content_length_allowed_ = value; }

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

  bool chunk_encoding_{true};
  bool processing_100_continue_{false};
  bool is_response_to_head_request_{false};
  bool is_content_length_allowed_{true};
};

/**
 * HTTP/1.1 response encoder.
 */
class ResponseStreamEncoderImpl : public StreamEncoderImpl {
public:
  ResponseStreamEncoderImpl(ConnectionImpl& connection) : StreamEncoderImpl(connection) {}

  bool startedResponse() { return started_response_; }

  // Http::StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;

private:
  bool started_response_{};
};

/**
 * HTTP/1.1 request encoder.
 */
class RequestStreamEncoderImpl : public StreamEncoderImpl {
public:
  RequestStreamEncoderImpl(ConnectionImpl& connection) : StreamEncoderImpl(connection) {}
  bool headRequest() { return head_request_; }

  // Http::StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;

private:
  bool head_request_{};
};

/**
 * Base class for HTTP/1.1 client and server connections.
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
   * Called when headers are encoded.
   */
  virtual void onEncodeHeaders(const HeaderMap& headers) PURE;

  /**
   * Called when resetStream() has been called on an active stream. In HTTP/1.1 the only
   * valid operation after this point is for the connection to get blown away, but we will not
   * fire any more callbacks in case some stack has to unwind.
   */
  void onResetStreamBase(StreamResetReason reason);

  /**
   * Flush all pending output from encoding.
   */
  void flushOutput();

  void addCharToBuffer(char c);
  void addIntToBuffer(uint64_t i);
  Buffer::WatermarkBuffer& buffer() { return output_buffer_; }
  uint64_t bufferRemainingSize();
  void copyToBuffer(const char* data, uint64_t length);
  void reserveBuffer(uint64_t size);

  // Http::Connection
  void dispatch(Buffer::Instance& data) override;
  void goAway() override {} // Called during connection manager drain flow
  Protocol protocol() override { return protocol_; }
  void shutdownNotice() override {} // Called during connection manager drain flow
  bool wantsToWrite() override { return false; }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override { onAboveHighWatermark(); }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override { onBelowLowWatermark(); }

  void readDisable(bool disable) { connection_.readDisable(disable); }
  uint32_t bufferLimit() { return connection_.bufferLimit(); }
  virtual bool supports_http_10() { return false; }

  bool maybeDirectDispatch(Buffer::Instance& data);

  CodecStats& stats() { return stats_; }

protected:
  ConnectionImpl(Network::Connection& connection, Stats::Scope& stats, http_parser_type type,
                 uint32_t max_request_headers_kb);

  bool resetStreamCalled() { return reset_stream_called_; }

  Network::Connection& connection_;
  CodecStats stats_;
  http_parser parser_;
  HeaderMapPtr deferred_end_stream_headers_;
  Http::Code error_code_{Http::Code::BadRequest};
  bool handling_upgrade_{};

private:
  enum class HeaderParsingState { Field, Value, Done };

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
   * invoked.
   * @return 0 if no error, 1 if there should be no body.
   */
  int onHeadersCompleteBase();
  virtual int onHeadersComplete(HeaderMapImplPtr&& headers) PURE;

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
  virtual void sendProtocolError() PURE;

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
  static const ToLowerTable& toLowerTable();

  HeaderMapImplPtr current_header_map_;
  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};
  HeaderString current_header_field_;
  HeaderString current_header_value_;
  bool reset_stream_called_{};
  Buffer::WatermarkBuffer output_buffer_;
  Buffer::RawSlice reserved_iovec_;
  char* reserved_current_{};
  Protocol protocol_{Protocol::Http11};
  const uint32_t max_headers_kb_;

  bool strict_header_validation_;
};

/**
 * Implementation of Http::ServerConnection for HTTP/1.1.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                       ServerConnectionCallbacks& callbacks, Http1Settings settings,
                       uint32_t max_request_headers_kb);

  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                       Http1Settings settings, uint32_t max_request_headers_kb,
                       bool strict_header_validation);

  bool supports_http_10() override { return codec_settings_.accept_http_10_; }

private:
  /**
   * An active HTTP/1.1 request.
   */
  struct ActiveRequest {
    ActiveRequest(ConnectionImpl& connection) : response_encoder_(connection) {}

    HeaderString request_url_;
    StreamDecoder* request_decoder_{};
    ResponseStreamEncoderImpl response_encoder_;
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
  void handlePath(HeaderMapImpl& headers, unsigned int method);

  // ConnectionImpl
  void onEncodeComplete() override;
  void onEncodeHeaders(const HeaderMap&) override {}
  void onMessageBegin() override;
  void onUrl(const char* data, size_t length) override;
  int onHeadersComplete(HeaderMapImplPtr&& headers) override;
  void onBody(const char* data, size_t length) override;
  void onMessageComplete() override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError() override;
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;

  ServerConnectionCallbacks& callbacks_;
  std::unique_ptr<ActiveRequest> active_request_;
  Http1Settings codec_settings_;
};

/**
 * Implementation of Http::ClientConnection for HTTP/1.1.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, Stats::Scope& stats,
                       ConnectionCallbacks& callbacks);

  // Http::ClientConnection
  StreamEncoder& newStream(StreamDecoder& response_decoder) override;

private:
  struct PendingResponse {
    PendingResponse(StreamDecoder* decoder) : decoder_(decoder) {}

    StreamDecoder* decoder_;
    bool head_request_{};
  };

  bool cannotHaveBody();

  // ConnectionImpl
  void onEncodeComplete() override {}
  void onEncodeHeaders(const HeaderMap& headers) override;
  void onMessageBegin() override {}
  void onUrl(const char*, size_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  int onHeadersComplete(HeaderMapImplPtr&& headers) override;
  void onBody(const char* data, size_t length) override;
  void onMessageComplete() override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError() override {}
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;

  std::unique_ptr<RequestStreamEncoderImpl> request_encoder_;
  std::list<PendingResponse> pending_responses_;
  // Set true between receiving 100-Continue headers and receiving the spurious onMessageComplete.
  bool ignore_message_complete_for_100_continue_{};

  // The default limit of 80 KiB is the vanilla http_parser behaviour.
  static constexpr uint32_t MAX_RESPONSE_HEADERS_KB = 80;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy

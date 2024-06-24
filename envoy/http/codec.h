#pragma once

#include <cstdint>
#include <limits>
#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_formatter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/protocol.h"
#include "envoy/http/stream_reset_handler.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/status.h"

namespace Envoy {
namespace Http {

enum class CodecType { HTTP1, HTTP2, HTTP3 };

namespace Http1 {
struct CodecStats;
}

namespace Http2 {
struct CodecStats;
}

namespace Http3 {
struct CodecStats;
}

// Legacy default value of 60K is safely under both codec default limits.
static constexpr uint32_t DEFAULT_MAX_REQUEST_HEADERS_KB = 60;
// Default maximum number of headers.
static constexpr uint32_t DEFAULT_MAX_HEADERS_COUNT = 100;

const char MaxRequestHeadersCountOverrideKey[] =
    "envoy.reloadable_features.max_request_headers_count";
const char MaxResponseHeadersCountOverrideKey[] =
    "envoy.reloadable_features.max_response_headers_count";
const char MaxRequestHeadersSizeOverrideKey[] =
    "envoy.reloadable_features.max_request_headers_size_kb";

class Stream;
class RequestDecoder;

/**
 * Error codes used to convey the reason for a GOAWAY.
 */
enum class GoAwayErrorCode {
  NoError,
  Other,
};

/**
 * Stream encoder options specific to HTTP/1.
 */
class Http1StreamEncoderOptions {
public:
  virtual ~Http1StreamEncoderOptions() = default;

  /**
   * Force disable chunk encoding, even if there is no known content length. This effectively forces
   * HTTP/1.0 behavior in which the connection will need to be closed to indicate end of stream.
   */
  virtual void disableChunkEncoding() PURE;
};

using Http1StreamEncoderOptionsOptRef =
    absl::optional<std::reference_wrapper<Http1StreamEncoderOptions>>;

/**
 * Encodes an HTTP stream. This interface contains methods common to both the request and response
 * path.
 * TODO(mattklein123): Consider removing the StreamEncoder interface entirely and just duplicating
 * the methods in both the request/response path for simplicity.
 */
class StreamEncoder {
public:
  virtual ~StreamEncoder() = default;

  /**
   * Encode a data frame.
   * @param data supplies the data to encode. The data may be moved by the encoder.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * @return Stream& the backing stream.
   */
  virtual Stream& getStream() PURE;

  /**
   * Encode metadata.
   * @param metadata_map_vector is the vector of metadata maps to encode.
   */
  virtual void encodeMetadata(const MetadataMapVector& metadata_map_vector) PURE;

  /**
   * Return the HTTP/1 stream encoder options if applicable. If the stream is not HTTP/1 returns
   * absl::nullopt.
   */
  virtual Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() PURE;
};

/**
 * Stream encoder used for sending a request (client to server). Virtual inheritance is required
 * due to a parallel implementation split between the shared base class and the derived class.
 */
class RequestEncoder : public virtual StreamEncoder {
public:
  /**
   * Encode headers, optionally indicating end of stream.
   * @param headers supplies the header map to encode. Must have required HTTP headers.
   * @param end_stream supplies whether this is a header only request.
   * @return Status indicating whether encoding succeeded. Encoding will fail if request
   * headers are missing required HTTP headers (method, path for non-CONNECT, host for CONNECT).
   */
  virtual Status encodeHeaders(const RequestHeaderMap& headers, bool end_stream) PURE;

  /**
   * Encode trailers. This implicitly ends the stream.
   * @param trailers supplies the trailers to encode.
   */
  virtual void encodeTrailers(const RequestTrailerMap& trailers) PURE;

  /**
   * Enable TCP Tunneling.
   */
  virtual void enableTcpTunneling() PURE;
};

/**
 * Stream encoder used for sending a response (server to client). Virtual inheritance is required
 * due to a parallel implementation split between the shared base class and the derived class.
 */
class ResponseEncoder : public virtual StreamEncoder {
public:
  /**
   * Encode supported 1xx headers.
   * Currently 100-Continue, 102-Processing, and 103-Early-Data headers are supported.
   * @param headers supplies the 1xx header map to encode.
   */
  virtual void encode1xxHeaders(const ResponseHeaderMap& headers) PURE;

  /**
   * Encode headers, optionally indicating end of stream. Response headers must
   * have a valid :status set.
   * @param headers supplies the header map to encode.
   * @param end_stream supplies whether this is a header only response.
   */
  virtual void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) PURE;

  /**
   * Encode trailers. This implicitly ends the stream.
   * @param trailers supplies the trailers to encode.
   */
  virtual void encodeTrailers(const ResponseTrailerMap& trailers) PURE;

  /**
   * Indicates whether invalid HTTP messaging should be handled with a stream error or a connection
   * error.
   */
  virtual bool streamErrorOnInvalidHttpMessage() const PURE;

  /**
   * Set a new request decoder for this ResponseEncoder. This is helpful in the case of an internal
   * redirect, in which a new request decoder is created in the context of the same downstream
   * request.
   * @param decoder new request decoder.
   */
  virtual void setRequestDecoder(RequestDecoder& decoder) PURE;

  /**
   * Set headers, trailers, and stream info for deferred logging. This allows HCM to hand off
   * stream-level details to the codec for logging after the stream may be destroyed (e.g. on
   * receiving the final ack packet from the client). Note that headers and trailers are const
   * as they will not be modified after this point.
   * @param request_header_map Request headers for this stream.
   * @param response_header_map Response headers for this stream.
   * @param response_trailer_map Response trailers for this stream.
   * @param stream_info Stream info for this stream.
   */
  virtual void
  setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr request_header_map,
                                       Http::ResponseHeaderMapConstSharedPtr response_header_map,
                                       Http::ResponseTrailerMapConstSharedPtr response_trailer_map,
                                       StreamInfo::StreamInfo& stream_info) PURE;
};

/**
 * Decodes an HTTP stream. These are callbacks fired into a sink. This interface contains methods
 * common to both the request and response path.
 * TODO(mattklein123): Consider removing the StreamDecoder interface entirely and just duplicating
 * the methods in both the request/response path for simplicity.
 */
class StreamDecoder {
public:
  virtual ~StreamDecoder() = default;

  /**
   * Called with a decoded data frame.
   * @param data supplies the decoded data.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void decodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called with decoded METADATA.
   * @param decoded METADATA.
   */
  virtual void decodeMetadata(MetadataMapPtr&& metadata_map) PURE;
};

/**
 * Stream decoder used for receiving a request (client to server). Virtual inheritance is required
 * due to a parallel implementation split between the shared base class and the derived class.
 */
class RequestDecoder : public virtual StreamDecoder {
public:
  /**
   * Called with decoded headers, optionally indicating end of stream.
   * @param headers supplies the decoded headers map.
   * @param end_stream supplies whether this is a header only request.
   */
  virtual void decodeHeaders(RequestHeaderMapSharedPtr&& headers, bool end_stream) PURE;

  /**
   * Called with a decoded trailers frame. This implicitly ends the stream.
   * @param trailers supplies the decoded trailers.
   */
  virtual void decodeTrailers(RequestTrailerMapPtr&& trailers) PURE;

  /**
   * Called if the codec needs to send a protocol error.
   * @param code supplies the HTTP error code to send.
   * @param body supplies an optional body to send with the local reply.
   * @param modify_headers supplies a way to edit headers before they are sent downstream.
   * @param grpc_status an optional gRPC status for gRPC requests
   * @param details details about the source of the error, for debug purposes
   */
  virtual void sendLocalReply(Code code, absl::string_view body,
                              const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                              const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                              absl::string_view details) PURE;

  /**
   * @return StreamInfo::StreamInfo& the stream_info for this stream.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * @return List of shared pointers to access loggers for this stream.
   */
  virtual std::list<AccessLog::InstanceSharedPtr> accessLogHandlers() PURE;
};

/**
 * Stream decoder used for receiving a response (server to client). Virtual inheritance is required
 * due to a parallel implementation split between the shared base class and the derived class.
 */
class ResponseDecoder : public virtual StreamDecoder {
public:
  /**
   * Called with decoded 1xx headers.
   * Currently 100-Continue, 102-Processing, and 103-Early-Data headers are supported.
   * @param headers supplies the decoded 1xx headers map.
   */
  virtual void decode1xxHeaders(ResponseHeaderMapPtr&& headers) PURE;

  /**
   * Called with decoded headers, optionally indicating end of stream.
   * @param headers supplies the decoded headers map.
   * @param end_stream supplies whether this is a header only response.
   */
  virtual void decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) PURE;

  /**
   * Called with a decoded trailers frame. This implicitly ends the stream.
   * @param trailers supplies the decoded trailers.
   */
  virtual void decodeTrailers(ResponseTrailerMapPtr&& trailers) PURE;

  /**
   * Dump the response decoder to the specified ostream.
   *
   * @param os the ostream to dump state to
   * @param indent_level the depth, for pretty-printing.
   *
   * This function is called on Envoy fatal errors so should avoid memory allocation.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

/**
 * Callbacks that fire against a stream.
 */
class StreamCallbacks {
public:
  virtual ~StreamCallbacks() = default;

  /**
   * Fires when a stream has been remote reset.
   * @param reason supplies the reset reason.
   * @param transport_failure_reason supplies underlying transport failure reason.
   */
  virtual void onResetStream(StreamResetReason reason,
                             absl::string_view transport_failure_reason) PURE;

  /**
   * Fires when a stream, or the connection the stream is sending to, goes over its high watermark.
   */
  virtual void onAboveWriteBufferHighWatermark() PURE;

  /**
   * Fires when a stream, or the connection the stream is sending to, goes from over its high
   * watermark to under its low watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() PURE;
};

/**
 * Codec event callbacks for a given HTTP Stream.
 * This can be used to tightly couple an entity with a streams low-level events.
 */
class CodecEventCallbacks {
public:
  virtual ~CodecEventCallbacks() = default;
  /**
   * Called when the the underlying codec finishes encoding.
   */
  virtual void onCodecEncodeComplete() PURE;

  /**
   * Called when the underlying codec has a low level reset.
   * e.g. Envoy serialized the response but it has not been flushed.
   */
  virtual void onCodecLowLevelReset() PURE;
};

/**
 * An HTTP stream (request, response, and push).
 */
class Stream : public StreamResetHandler {
public:
  /**
   * Add stream callbacks.
   * @param callbacks supplies the callbacks to fire on stream events.
   */
  virtual void addCallbacks(StreamCallbacks& callbacks) PURE;

  /**
   * Remove stream callbacks.
   * @param callbacks supplies the callbacks to remove.
   */
  virtual void removeCallbacks(StreamCallbacks& callbacks) PURE;

  /**
   * Register the codec event callbacks for this stream.
   * The stream can only have a single registered callback at a time.
   * @param codec_callbacks the codec callbacks for this stream.
   * @return CodecEventCallbacks* the prior registered codec callbacks.
   */
  virtual CodecEventCallbacks*
  registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) PURE;

  /**
   * Enable/disable further data from this stream.
   * Cessation of data may not be immediate. For example, for HTTP/2 this may stop further flow
   * control window updates which will result in the peer eventually stopping sending data.
   * @param disable informs if reads should be disabled (true) or re-enabled (false).
   *
   * Note that this function reference counts calls. For example
   * readDisable(true);  // Disables data
   * readDisable(true);  // Notes the stream is blocked by two sources
   * readDisable(false);  // Notes the stream is blocked by one source
   * readDisable(false);  // Marks the stream as unblocked, so resumes reading.
   */
  virtual void readDisable(bool disable) PURE;

  /**
   * Return the number of bytes this stream is allowed to buffer, or 0 if there is no limit
   * configured.
   * @return uint32_t the stream's configured buffer limits.
   */
  virtual uint32_t bufferLimit() const PURE;

  /**
   * @return string_view optionally return the reason behind codec level errors.
   *
   * This information is communicated via direct accessor rather than passed with the
   * CodecProtocolException so that the error can be associated only with the problematic stream and
   * not associated with every stream on the connection.
   */
  virtual absl::string_view responseDetails() { return ""; }

  /**
   * @return const Network::ConnectionInfoProvider& the adderess provider  of the connection
   * associated with the stream.
   */
  virtual const Network::ConnectionInfoProvider& connectionInfoProvider() PURE;

  /**
   * Set the flush timeout for the stream. At the codec level this is used to bound the amount of
   * time the codec will wait to flush body data pending open stream window. It does *not* count
   * small window updates as satisfying the idle timeout as this is a potential DoS vector.
   */
  virtual void setFlushTimeout(std::chrono::milliseconds timeout) PURE;

  /**
   * @return the account, if any, used by this stream.
   */
  virtual Buffer::BufferMemoryAccountSharedPtr account() const PURE;

  /**
   * Sets the account for this stream, propagating it to all of its buffers.
   * @param the account to assign this stream.
   */
  virtual void setAccount(Buffer::BufferMemoryAccountSharedPtr account) PURE;

  /**
   * Get the bytes meter for this stream.
   */
  virtual const StreamInfo::BytesMeterSharedPtr& bytesMeter() PURE;
};

/**
 * A class for sharing what HTTP/2 SETTINGS were received from the peer.
 */
class ReceivedSettings {
public:
  virtual ~ReceivedSettings() = default;

  /**
   * @return value of SETTINGS_MAX_CONCURRENT_STREAMS, or absl::nullopt if it was not present.
   */
  virtual const absl::optional<uint32_t>& maxConcurrentStreams() const PURE;
};

/**
 * Connection level callbacks.
 */
class ConnectionCallbacks {
public:
  virtual ~ConnectionCallbacks() = default;

  /**
   * Fires when the remote indicates "go away." No new streams should be created.
   */
  virtual void onGoAway(GoAwayErrorCode error_code) PURE;

  /**
   * Fires when the peer settings frame is received from the peer.
   * This may occur multiple times across the lifetime of the connection.
   * @param ReceivedSettings the settings received from the peer.
   */
  virtual void onSettings(ReceivedSettings& settings) { UNREFERENCED_PARAMETER(settings); }

  /**
   * Fires when the MAX_STREAMS frame is received from the peer.
   * This is an HTTP/3 frame, indicating the new maximum stream ID which can be opened.
   * This may occur multiple times across the lifetime of an HTTP/3 connection.
   * @param num_streams the number of streams now allowed to be opened.
   */
  virtual void onMaxStreamsChanged(uint32_t num_streams) { UNREFERENCED_PARAMETER(num_streams); }
};

/**
 * HTTP/1.* Codec settings
 */
struct Http1Settings {
  // Enable codec to parse absolute URIs. This enables forward/explicit proxy support for non TLS
  // traffic
  bool allow_absolute_url_{false};
  // Allow HTTP/1.0 from downstream.
  bool accept_http_10_{false};
  // Set a default host if no Host: header is present for HTTP/1.0 requests.`
  std::string default_host_for_http_10_;
  // Encode trailers in Http. By default the HTTP/1 codec drops proxied trailers.
  // Note that this only happens when Envoy is chunk encoding which occurs when:
  //  - The request is HTTP/1.1
  //  - Is neither a HEAD only request nor a HTTP Upgrade
  //  - Not a HEAD request
  bool enable_trailers_{false};
  // Allows Envoy to process requests/responses with both `Content-Length` and `Transfer-Encoding`
  // headers set. By default such messages are rejected, but if option is enabled - Envoy will
  // remove Content-Length header and process message.
  bool allow_chunked_length_{false};

  enum class HeaderKeyFormat {
    // By default no formatting is performed, presenting all headers in lowercase (as Envoy
    // internals normalize everything to lowercase.)
    Default,
    // Performs proper casing of header keys: the first and all alpha characters following a
    // non-alphanumeric character is capitalized.
    ProperCase,
    // A stateful formatter extension has been configured.
    StatefulFormatter,
  };

  // How header keys should be formatted when serializing HTTP/1.1 headers.
  HeaderKeyFormat header_key_format_{HeaderKeyFormat::Default};

  // Non-null IFF header_key_format_ is configured to StatefulFormatter.
  StatefulHeaderKeyFormatterFactorySharedPtr stateful_header_key_formatter_;

  // Behaviour on invalid HTTP messaging:
  // - if true, the HTTP/1.1 connection is left open (where possible)
  // - if false, the HTTP/1.1 connection is terminated
  bool stream_error_on_invalid_http_message_{false};

  // True if this is an edge Envoy (using downstream address, no trusted hops)
  // and https:// URLs should be rejected over unencrypted connections.
  bool validate_scheme_{false};

  // If true, Envoy will send a fully qualified URL in the firstline of the request.
  bool send_fully_qualified_url_{false};

  // If true, BalsaParser is used for HTTP/1 parsing; if false, http-parser is
  // used. See issue #21245.
  bool use_balsa_parser_{false};

  // If true, any non-empty method composed of valid characters is accepted.
  // If false, only methods from a hard-coded list of known methods are accepted.
  // Only implemented in BalsaParser. http-parser only accepts known methods.
  bool allow_custom_methods_{false};
};

/**
 * A connection (client or server) that owns multiple streams.
 */
class Connection {
public:
  virtual ~Connection() = default;

  /**
   * Dispatch incoming connection data.
   * @param data supplies the data to dispatch. The codec will drain as many bytes as it processes.
   * @return Status indicating the status of the codec. Holds any errors encountered while
   * processing the incoming data.
   */
  virtual Status dispatch(Buffer::Instance& data) PURE;

  /**
   * Indicate "go away" to the remote. No new streams can be created beyond this point.
   */
  virtual void goAway() PURE;

  /**
   * @return the protocol backing the connection. This can change if for example an HTTP/1.1
   *         connection gets an HTTP/1.0 request on it.
   */
  virtual Protocol protocol() PURE;

  /**
   * Indicate a "shutdown notice" to the remote. This is a hint that the remote should not send
   * any new streams, but if streams do arrive that will not be reset.
   */
  virtual void shutdownNotice() PURE;

  /**
   * @return bool whether the codec has data that it wants to write but cannot due to protocol
   *              reasons (e.g, needing window updates).
   */
  virtual bool wantsToWrite() PURE;

  /**
   * Called when the underlying Network::Connection goes over its high watermark.
   */
  virtual void onUnderlyingConnectionAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the underlying Network::Connection goes from over its high watermark to under its
   * low watermark.
   */
  virtual void onUnderlyingConnectionBelowWriteBufferLowWatermark() PURE;
};

/**
 * Callbacks for downstream connection watermark limits.
 */
class DownstreamWatermarkCallbacks {
public:
  virtual ~DownstreamWatermarkCallbacks() = default;

  /**
   * Called when the downstream connection or stream goes over its high watermark. Note that this
   * may be called separately for both the stream going over and the connection going over. It
   * is the responsibility of the DownstreamWatermarkCallbacks implementation to handle unwinding
   * multiple high and low watermark calls.
   */
  virtual void onAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the downstream connection or stream goes from over its high watermark to under its
   * low watermark. As with onAboveWriteBufferHighWatermark above, this may be called independently
   * when both the stream and the connection go under the low watermark limit, and the callee must
   * ensure that the flow of data does not resume until all callers which were above their high
   * watermarks have gone below.
   */
  virtual void onBelowWriteBufferLowWatermark() PURE;
};

/**
 * Callbacks for server connections.
 */
class ServerConnectionCallbacks : public virtual ConnectionCallbacks {
public:
  /**
   * Invoked when a new request stream is initiated by the remote.
   * @param response_encoder supplies the encoder to use for creating the response. The request and
   *                         response are backed by the same Stream object.
   * @param is_internally_created indicates if this stream was originated by a
   *   client, or was created by Envoy, by example as part of an internal redirect.
   * @return RequestDecoder& supplies the decoder callbacks to fire into for stream decoding
   *   events.
   */
  virtual RequestDecoder& newStream(ResponseEncoder& response_encoder,
                                    bool is_internally_created = false) PURE;
};

/**
 * A server side HTTP connection.
 */
class ServerConnection : public virtual Connection {};
using ServerConnectionPtr = std::unique_ptr<ServerConnection>;

/**
 * A client side HTTP connection.
 */
class ClientConnection : public virtual Connection {
public:
  /**
   * Create a new outgoing request stream.
   * @param response_decoder supplies the decoder callbacks to fire response events into.
   * @return RequestEncoder& supplies the encoder to write the request into.
   */
  virtual RequestEncoder& newStream(ResponseDecoder& response_decoder) PURE;
};

using ClientConnectionPtr = std::unique_ptr<ClientConnection>;

} // namespace Http
} // namespace Envoy

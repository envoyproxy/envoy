#pragma once

#include <cstdint>
#include <limits>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/protocol.h"

namespace Envoy {
namespace Http {

// Legacy default value of 60K is safely under both codec default limits.
static const uint32_t DEFAULT_MAX_REQUEST_HEADERS_KB = 60;
// Default maximum number of headers.
static const uint32_t DEFAULT_MAX_HEADERS_COUNT = 100;

const char MaxRequestHeadersCountOverrideKey[] =
    "envoy.reloadable_features.max_request_headers_count";
const char MaxResponseHeadersCountOverrideKey[] =
    "envoy.reloadable_features.max_response_headers_count";

class Stream;

/**
 * Encodes an HTTP stream.
 */
class StreamEncoder {
public:
  virtual ~StreamEncoder() = default;

  /**
   * Encode 100-Continue headers.
   * @param headers supplies the 100-Continue header map to encode.
   */
  virtual void encode100ContinueHeaders(const HeaderMap& headers) PURE;

  /**
   * Encode headers, optionally indicating end of stream. Response headers must
   * have a valid :status set.
   * @param headers supplies the header map to encode.
   * @param end_stream supplies whether this is a header only request/response.
   */
  virtual void encodeHeaders(const HeaderMap& headers, bool end_stream) PURE;

  /**
   * Encode a data frame.
   * @param data supplies the data to encode. The data may be moved by the encoder.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Encode trailers. This implicitly ends the stream.
   * @param trailers supplies the trailers to encode.
   */
  virtual void encodeTrailers(const HeaderMap& trailers) PURE;

  /**
   * @return Stream& the backing stream.
   */
  virtual Stream& getStream() PURE;

  /**
   * Encode metadata.
   * @param metadata_map_vector is the vector of metadata maps to encode.
   */
  virtual void encodeMetadata(const MetadataMapVector& metadata_map_vector) PURE;
};

/**
 * Decodes an HTTP stream. These are callbacks fired into a sink.
 */
class StreamDecoder {
public:
  virtual ~StreamDecoder() = default;

  /**
   * Called with decoded 100-Continue headers.
   * @param headers supplies the decoded 100-Continue headers map that is moved into the callee.
   */
  virtual void decode100ContinueHeaders(HeaderMapPtr&& headers) PURE;

  /**
   * Called with decoded headers, optionally indicating end of stream.
   * @param headers supplies the decoded headers map that is moved into the callee.
   * @param end_stream supplies whether this is a header only request/response.
   */
  virtual void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) PURE;

  /**
   * Called with a decoded data frame.
   * @param data supplies the decoded data.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void decodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called with a decoded trailers frame. This implicitly ends the stream.
   * @param trailers supplies the decoded trailers.
   */
  virtual void decodeTrailers(HeaderMapPtr&& trailers) PURE;

  /**
   * Called with decoded METADATA.
   * @param decoded METADATA.
   */
  virtual void decodeMetadata(MetadataMapPtr&& metadata_map) PURE;
};

/**
 * Stream reset reasons.
 */
enum class StreamResetReason {
  // If a local codec level reset was sent on the stream.
  LocalReset,
  // If a local codec level refused stream reset was sent on the stream (allowing for retry).
  LocalRefusedStreamReset,
  // If a remote codec level reset was received on the stream.
  RemoteReset,
  // If a remote codec level refused stream reset was received on the stream (allowing for retry).
  RemoteRefusedStreamReset,
  // If the stream was locally reset by a connection pool due to an initial connection failure.
  ConnectionFailure,
  // If the stream was locally reset due to connection termination.
  ConnectionTermination,
  // The stream was reset because of a resource overflow.
  Overflow
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
 * An HTTP stream (request, response, and push).
 */
class Stream {
public:
  virtual ~Stream() = default;

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
   * Reset the stream. No events will fire beyond this point.
   * @param reason supplies the reset reason.
   */
  virtual void resetStream(StreamResetReason reason) PURE;

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

  /*
   * Return the number of bytes this stream is allowed to buffer, or 0 if there is no limit
   * configured.
   * @return uint32_t the stream's configured buffer limits.
   */
  virtual uint32_t bufferLimit() PURE;
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
  virtual void onGoAway() PURE;
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

  enum class HeaderKeyFormat {
    // By default no formatting is performed, presenting all headers in lowercase (as Envoy
    // internals normalize everything to lowercase.)
    Default,
    // Performs proper casing of header keys: the first and all alpha characters following a
    // non-alphanumeric character is capitalized.
    ProperCase,
  };

  // How header keys should be formatted when serializing HTTP/1.1 headers.
  HeaderKeyFormat header_key_format_;
};

/**
 * HTTP/2 codec settings
 */
struct Http2Settings {
  // TODO(jwfang): support other HTTP/2 settings
  uint32_t hpack_table_size_{DEFAULT_HPACK_TABLE_SIZE};
  uint32_t max_concurrent_streams_{DEFAULT_MAX_CONCURRENT_STREAMS};
  uint32_t initial_stream_window_size_{DEFAULT_INITIAL_STREAM_WINDOW_SIZE};
  uint32_t initial_connection_window_size_{DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE};
  bool allow_connect_{DEFAULT_ALLOW_CONNECT};
  bool allow_metadata_{DEFAULT_ALLOW_METADATA};
  bool stream_error_on_invalid_http_messaging_{DEFAULT_STREAM_ERROR_ON_INVALID_HTTP_MESSAGING};
  uint32_t max_outbound_frames_{DEFAULT_MAX_OUTBOUND_FRAMES};
  uint32_t max_outbound_control_frames_{DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES};
  uint32_t max_consecutive_inbound_frames_with_empty_payload_{
      DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD};
  uint32_t max_inbound_priority_frames_per_stream_{DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM};
  uint32_t max_inbound_window_update_frames_per_data_frame_sent_{
      DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT};

  // disable HPACK compression
  static const uint32_t MIN_HPACK_TABLE_SIZE = 0;
  // initial value from HTTP/2 spec, same as NGHTTP2_DEFAULT_HEADER_TABLE_SIZE from nghttp2
  static const uint32_t DEFAULT_HPACK_TABLE_SIZE = (1 << 12);
  // no maximum from HTTP/2 spec, use unsigned 32-bit maximum
  static const uint32_t MAX_HPACK_TABLE_SIZE = std::numeric_limits<uint32_t>::max();

  // TODO(jwfang): make this 0, the HTTP/2 spec minimum
  static const uint32_t MIN_MAX_CONCURRENT_STREAMS = 1;
  // defaults to maximum, same as nghttp2
  static const uint32_t DEFAULT_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;
  // no maximum from HTTP/2 spec, total streams is unsigned 32-bit maximum,
  // one-side (client/server) is half that, and we need to exclude stream 0.
  // same as NGHTTP2_INITIAL_MAX_CONCURRENT_STREAMS from nghttp2
  static const uint32_t MAX_MAX_CONCURRENT_STREAMS = (1U << 31) - 1;

  // initial value from HTTP/2 spec, same as NGHTTP2_INITIAL_WINDOW_SIZE from nghttp2
  // NOTE: we only support increasing window size now, so this is also the minimum
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_STREAM_WINDOW_SIZE = (1 << 16) - 1;
  // initial value from HTTP/2 spec is 65535, but we want more (256MiB)
  static const uint32_t DEFAULT_INITIAL_STREAM_WINDOW_SIZE = 256 * 1024 * 1024;
  // maximum from HTTP/2 spec, same as NGHTTP2_MAX_WINDOW_SIZE from nghttp2
  static const uint32_t MAX_INITIAL_STREAM_WINDOW_SIZE = (1U << 31) - 1;

  // CONNECTION_WINDOW_SIZE is similar to STREAM_WINDOW_SIZE, but for connection-level window
  // TODO(jwfang): make this 0 to support decrease window size
  static const uint32_t MIN_INITIAL_CONNECTION_WINDOW_SIZE = (1 << 16) - 1;
  // nghttp2's default connection-level window equals to its stream-level,
  // our default connection-level window also equals to our stream-level
  static const uint32_t DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE = 256 * 1024 * 1024;
  static const uint32_t MAX_INITIAL_CONNECTION_WINDOW_SIZE = (1U << 31) - 1;
  // By default both nghttp2 and Envoy do not allow CONNECT over H2.
  static const bool DEFAULT_ALLOW_CONNECT = false;
  // By default Envoy does not allow METADATA support.
  static const bool DEFAULT_ALLOW_METADATA = false;
  // By default Envoy does not allow invalid headers.
  static const bool DEFAULT_STREAM_ERROR_ON_INVALID_HTTP_MESSAGING = false;

  // Default limit on the number of outbound frames of all types.
  static const uint32_t DEFAULT_MAX_OUTBOUND_FRAMES = 10000;
  // Default limit on the number of outbound frames of types PING, SETTINGS and RST_STREAM.
  static const uint32_t DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES = 1000;
  // Default limit on the number of consecutive inbound frames with an empty payload
  // and no end stream flag.
  static const uint32_t DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD = 1;
  // Default limit on the number of inbound frames of type PRIORITY (per stream).
  static const uint32_t DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM = 100;
  // Default limit on the number of inbound frames of type WINDOW_UPDATE (per DATA frame sent).
  static const uint32_t DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT = 10;
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
   */
  virtual void dispatch(Buffer::Instance& data) PURE;

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
   * @return StreamDecoder& supplies the decoder callbacks to fire into for stream decoding events.
   */
  virtual StreamDecoder& newStream(StreamEncoder& response_encoder,
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
   * @return StreamEncoder& supplies the encoder to write the request into.
   */
  virtual StreamEncoder& newStream(StreamDecoder& response_decoder) PURE;
};

using ClientConnectionPtr = std::unique_ptr<ClientConnection>;

} // namespace Http
} // namespace Envoy

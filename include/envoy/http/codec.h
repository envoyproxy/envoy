#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

namespace Http {

class Stream;

/**
 * Encodes an HTTP stream.
 * NOTE: Currently we do not support trailers/intermediate header frames.
 */
class StreamEncoder {
public:
  virtual ~StreamEncoder() {}

  /**
   * Encode headers, optionally indicating end of stream.
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
};

/**
 * Decodes an HTTP stream. These are callbacks fired into a sink.
 * NOTE: Currently we do not support trailers/intermediate header frames.
 */
class StreamDecoder {
public:
  virtual ~StreamDecoder() {}

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
  virtual ~StreamCallbacks() {}

  /**
   * Fires when a stream has been remote reset.
   * @param reason supplies the reset reason.
   */
  virtual void onResetStream(StreamResetReason reason) PURE;
};

/**
 * An HTTP stream (request, response, and push).
 */
class Stream {
public:
  virtual ~Stream() {}

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
};

/**
 * Connection level callbacks.
 */
class ConnectionCallbacks {
public:
  virtual ~ConnectionCallbacks() {}

  /**
   * Fires when the remote indicates "go away." No new streams should be created.
   */
  virtual void onGoAway() PURE;
};

/**
 * A list of features that a codec provides.
 */
class CodecFeatures {
public:
  static const uint64_t Multiplexing = 0x1;
};

/**
 * A list of options that can be specified when creating a codec.
 */
class CodecOptions {
public:
  static const uint64_t NoCompression = 0x1;
};

/**
 * A connection (client or server) that owns multiple streams.
 */
class Connection {
public:
  virtual ~Connection() {}

  /**
   * Dispatch incoming connection data.
   * @param data supplies the data to dispatch. The codec will drain as many bytes as it processes.
   */
  virtual void dispatch(Buffer::Instance& data) PURE;

  /**
   * Get the features that a connection provides. Maps to entries in CodecFeatures.
   */
  virtual uint64_t features() PURE;

  /**
   * Indicate "go away" to the remote. No new streams can be created beyond this point.
   */
  virtual void goAway() PURE;

  /**
   * @return const std::string& the human readable name of the protocol that this codec wraps.
   */
  virtual const std::string& protocolString() PURE;

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
   * @return StreamDecoder& supplies the decoder callbacks to fire into for stream decoding events.
   */
  virtual StreamDecoder& newStream(StreamEncoder& response_encoder) PURE;
};

/**
 * A server side HTTP connection.
 */
class ServerConnection : public virtual Connection {};

typedef std::unique_ptr<ServerConnection> ServerConnectionPtr;

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

typedef std::unique_ptr<ClientConnection> ClientConnectionPtr;

} // Http

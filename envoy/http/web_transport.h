#pragma once

#include <cstddef>
#include <cstdint>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Http {

/**
 * Result of a read from a WebTransport stream.
 */
struct WebTransportStreamReadResult {
  size_t bytes_read;
  bool end_stream;
};

/**
 * Application callbacks for a single WebTransport stream.
 */
class WebTransportStreamCallbacks {
public:
  virtual ~WebTransportStreamCallbacks() = default;

  /**
   * Called when the stream has data to read or has reached its end.
   */
  virtual void onWebTransportStreamData() PURE;

  /**
   * Called when the stream can accept writes again after a blocked write.
   */
  virtual void onWebTransportStreamCanWrite() PURE;

  /**
   * Called when the peer reset the stream with the given error code.
   */
  virtual void onWebTransportStreamReset(uint32_t error_code) PURE;

  /**
   * Called when the peer asked to stop sending with the given error code.
   */
  virtual void onWebTransportStreamStopSending(uint32_t error_code) PURE;
};

/**
 * Application surface for a single WebTransport stream. The QUIC layer adapts the vendored QUICHE
 * stream so consumers avoid QUICHE headers. The session owns the handle and it is valid until the
 * stream closes.
 */
class WebTransportStream {
public:
  virtual ~WebTransportStream() = default;

  /**
   * Whether the stream is bidirectional.
   */
  virtual bool bidirectional() const PURE;

  /**
   * Registers stream-event callbacks. A nullptr detaches the consumer.
   */
  virtual void setWebTransportStreamCallbacks(WebTransportStreamCallbacks* callbacks) PURE;

  /**
   * Reads up to buffer.size() bytes into buffer. The result reports the byte count read and whether
   * the end of the stream was reached.
   */
  virtual WebTransportStreamReadResult readWebTransportStream(absl::Span<char> buffer) PURE;

  /**
   * Writes the data, optionally ending the stream. Returns false if the stream cannot accept it
   * now, in which case the caller retries after onWebTransportStreamCanWrite().
   */
  virtual bool writeWebTransportStream(absl::string_view data, bool end_stream) PURE;

  /**
   * Whether the stream can accept a write now.
   */
  virtual bool canWriteWebTransportStream() const PURE;

  /**
   * Resets the write side with the given error code.
   */
  virtual void resetWebTransportStream(uint32_t error_code) PURE;

  /**
   * Asks the peer to stop sending with the given error code.
   */
  virtual void stopSendingWebTransportStream(uint32_t error_code) PURE;
};

/**
 * Application callbacks for a terminated WebTransport session. Implemented by the HTTP filter.
 */
class WebTransportSessionCallbacks {
public:
  virtual ~WebTransportSessionCallbacks() = default;

  /**
   * Called once the session is ready. Delivered synchronously while the accepting response is
   * encoded.
   */
  virtual void onWebTransportSessionReady() PURE;

  /**
   * Called for each received datagram. The view is valid only during the call.
   */
  virtual void onWebTransportDatagram(absl::string_view datagram) PURE;

  /**
   * Called once when the session closes. No further callbacks or calls are valid afterward.
   */
  virtual void onWebTransportSessionClosed() PURE;

  /**
   * Called when the peer opened a stream. The handle is owned by the session.
   */
  virtual void onWebTransportStreamIncoming(WebTransportStream& stream, bool bidirectional) PURE;

  /**
   * Called when a new outgoing stream of the given kind can be opened after a flow control block.
   */
  virtual void onCanCreateWebTransportStream(bool bidirectional) PURE;
};

/**
 * Application surface for a terminated WebTransport session. The QUIC layer adapts the vendored
 * QUICHE session so consumers avoid QUICHE headers. Owned by the codec stream and valid until
 * onWebTransportSessionClosed() or filter destruction.
 */
class WebTransportSession {
public:
  virtual ~WebTransportSession() = default;

  /**
   * Whether the connection is already at its WebTransport session limit. A consumer must reject the
   * request instead of claiming the session when this is true.
   */
  virtual bool sessionLimitExceeded() const PURE;

  /**
   * Registers session-event callbacks. A nullptr detaches the consumer.
   */
  virtual void setWebTransportSessionCallbacks(WebTransportSessionCallbacks* callbacks) PURE;

  /**
   * Sends a datagram on the session. Datagrams are unreliable and may be dropped.
   */
  virtual void sendWebTransportDatagram(absl::string_view datagram) PURE;

  /**
   * Whether a new outgoing stream of the given kind can be opened now.
   */
  virtual bool canOpenWebTransportStream(bool bidirectional) const PURE;

  /**
   * Opens an outgoing stream, or returns nullptr if flow control blocks it. The session owns the
   * returned handle.
   */
  virtual WebTransportStream* openWebTransportStream(bool bidirectional) PURE;
};

} // namespace Http
} // namespace Envoy

#pragma once

#include "envoy/common/pure.h"

// Stream Reset is refactored from the codec to avoid cyclical dependencies with
// the BufferMemoryAccount interface.
namespace Envoy {
namespace Http {

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
  Overflow,
  // Either there was an early TCP error for a CONNECT request or the peer reset with CONNECT_ERROR
  ConnectError,
  // Received payload did not conform to HTTP protocol.
  ProtocolError,
  // If the stream was locally reset by the Overload Manager.
  OverloadManager
};

/**
 * Handler to reset an underlying HTTP stream.
 */
class StreamResetHandler {
public:
  virtual ~StreamResetHandler() = default;
  /**
   * Reset the stream. No events will fire beyond this point.
   * @param reason supplies the reset reason.
   */
  virtual void resetStream(StreamResetReason reason) PURE;
};

} // namespace Http
} // namespace Envoy

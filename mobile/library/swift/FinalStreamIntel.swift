@_implementationOnly import EnvoyEngine
import Foundation

/// Exposes one time HTTP stream metrics, context, and other details.
/// Note: -1 means "not present" for the fields of type Int64.
@objcMembers
public final class FinalStreamIntel: StreamIntel {
  /// The time the request started, in ms since the epoch.
  public let requestStartMs: Int64
  /// The time the DNS resolution for this request started, in ms since the epoch.
  public let dnsStartMs: Int64
  /// The time the DNS resolution for this request completed, in ms since the epoch.
  public let dnsEndMs: Int64
  /// The time the upstream connection started, in ms since the epoch. (1)
  public let connectStartMs: Int64
  /// The time the upstream connection completed, in ms since the epoch. (1)
  public let connectEndMs: Int64
  /// The time the SSL handshake started, in ms since the epoch. (1)
  public let sslStartMs: Int64
  /// The time the SSL handshake completed, in ms since the epoch. (1)
  public let sslEndMs: Int64
  /// The time the first byte of the request was sent upstream, in ms since the epoch.
  public let sendingStartMs: Int64
  /// The time the last byte of the request was sent upstream, in ms since the epoch.
  public let sendingEndMs: Int64
  /// The time the first byte of the response was received, in ms since the epoch.
  public let responseStartMs: Int64
  /// The time the last byte of the request was received, in ms since the epoch.
  public let requestEndMs: Int64
  /// True if the upstream socket had been used previously.
  public let socketReused: Bool
  /// The number of bytes sent upstream.
  public let sentByteCount: UInt64
  /// The number of bytes received from upstream.
  public let receivedByteCount: UInt64
  /// The response flags for the upstream stream.
  public let responseFlags: UInt64

  // NOTE(1): These fields may not be set if socket_reused is false.

  public init(
    streamId: Int64,
    connectionId: Int64,
    attemptCount: UInt64,
    requestStartMs: Int64,
    dnsStartMs: Int64,
    dnsEndMs: Int64,
    connectStartMs: Int64,
    connectEndMs: Int64,
    sslStartMs: Int64,
    sslEndMs: Int64,
    sendingStartMs: Int64,
    sendingEndMs: Int64,
    responseStartMs: Int64,
    requestEndMs: Int64,
    socketReused: Bool,
    sentByteCount: UInt64,
    receivedByteCount: UInt64,
    responseFlags: UInt64
  ) {
    self.requestStartMs = requestStartMs
    self.dnsStartMs = dnsStartMs
    self.dnsEndMs = dnsEndMs
    self.connectStartMs = connectStartMs
    self.connectEndMs = connectEndMs
    self.sslStartMs = sslStartMs
    self.sslEndMs = sslEndMs
    self.sendingStartMs = sendingStartMs
    self.sendingEndMs = sendingEndMs
    self.responseStartMs = responseStartMs
    self.requestEndMs = requestEndMs
    self.socketReused = socketReused
    self.sentByteCount = sentByteCount
    self.receivedByteCount = receivedByteCount
    self.responseFlags = responseFlags
    super.init(streamId: streamId, connectionId: connectionId, attemptCount: attemptCount)
  }
}

extension FinalStreamIntel {
  internal convenience init(_ cIntel: EnvoyStreamIntel, _ cFinalIntel: EnvoyFinalStreamIntel) {
    self.init(
      streamId: cIntel.stream_id,
      connectionId: cIntel.connection_id,
      attemptCount: cIntel.attempt_count,
      requestStartMs: cFinalIntel.request_start_ms,
      dnsStartMs: cFinalIntel.dns_start_ms,
      dnsEndMs: cFinalIntel.dns_end_ms,
      connectStartMs: cFinalIntel.connect_start_ms,
      connectEndMs: cFinalIntel.connect_end_ms,
      sslStartMs: cFinalIntel.ssl_start_ms,
      sslEndMs: cFinalIntel.ssl_end_ms,
      sendingStartMs: cFinalIntel.sending_start_ms,
      sendingEndMs: cFinalIntel.sending_end_ms,
      responseStartMs: cFinalIntel.response_start_ms,
      requestEndMs: cFinalIntel.request_end_ms,
      socketReused: cFinalIntel.socket_reused != 0,
      sentByteCount: cFinalIntel.sent_byte_count,
      receivedByteCount: cFinalIntel.received_byte_count,
      responseFlags: cFinalIntel.response_flags
    )
  }
}

@_implementationOnly import EnvoyEngine
import Foundation

/// Exposes internal HTTP stream metrics, context, and other details.
@objcMembers
public final class StreamIntel: NSObject, Error {
  // An internal identifier for the stream.
  public let streamId: UInt64
  // An internal identifier for the connection carrying the stream.
  public let connectionId: UInt64
  // The number of internal attempts to carry out a request/operation.
  public let attemptCount: UInt64

  public init(streamId: UInt64, connectionId: UInt64, attemptCount: UInt64) {
    self.streamId = streamId
    self.connectionId = connectionId
    self.attemptCount = attemptCount
  }
}

extension StreamIntel {
  internal convenience init(_ cStruct: EnvoyStreamIntel) {
    self.init(streamId: cStruct.stream_id,
              connectionId: cStruct.connection_id,
              attemptCount: cStruct.attempt_count)
  }
}

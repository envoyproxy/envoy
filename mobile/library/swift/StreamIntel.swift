@_implementationOnly import EnvoyEngine
import Foundation

/// Exposes internal HTTP stream metrics, context, and other details.
@objcMembers
public class StreamIntel: NSObject, Error {
  // An internal identifier for the stream. -1 if not set.
  public let streamId: Int64
  // An internal identifier for the connection carrying the stream. -1 if not set.
  public let connectionId: Int64
  // The number of internal attempts to carry out a request/operation. 0 if not set.
  public let attemptCount: UInt64

  public init(streamId: Int64, connectionId: Int64, attemptCount: UInt64) {
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

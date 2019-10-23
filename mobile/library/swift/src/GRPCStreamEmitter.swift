import Foundation

/// gRPC prefix length: 1 byte for compression and 4 bytes for message length.
let kGRPCPrefixLength: Int = 5

/// Emitter that allows for sending additional data over gRPC.
@objcMembers
public final class GRPCStreamEmitter: NSObject {
  private let underlyingEmitter: StreamEmitter

  // MARK: - Internal

  /// Initialize a new emitter.
  ///
  /// - parameter emitter: The underlying stream emitter to use for sending data.
  init(emitter: StreamEmitter) {
    self.underlyingEmitter = emitter
  }

  // MARK: - Public

  /// Send a protobuf message's binary data over the gRPC stream.
  ///
  /// - parameter messageData: Binary data of a protobuf message to send.
  ///
  /// - returns: The stream emitter, for chaining syntax.
  @discardableResult
  public func sendMessage(_ messageData: Data) -> GRPCStreamEmitter {
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests
    // Length-Prefixed-Message = Compressed-Flag | Message-Length | Message
    // Compressed-Flag = 0 / 1, encoded as 1 byte unsigned integer
    // Message-Length = length of Message, encoded as 4 byte unsigned integer (big endian)
    // Message = binary representation of protobuf message
    var prefixData = Data(capacity: kGRPCPrefixLength)

    // Compression flag (1 byte) - 0, not compressed
    prefixData.append(0)

    // Message length (4 bytes)
    var length = UInt32(messageData.count).bigEndian
    prefixData.append(UnsafeBufferPointer(start: &length, count: 1))

    // Send prefix data followed by message data
    self.underlyingEmitter.sendData(prefixData)
    self.underlyingEmitter.sendData(messageData)
    return self
  }

  /// Close this connection.
  public func close() {
    // The gRPC protocol requires the client stream to close with a DATA frame.
    // More information here:
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests
    self.underlyingEmitter.close(trailers: nil)
  }
}

import Foundation

/// gRPC prefix length: 1 byte for compression and 4 bytes for message length.
let kGRPCPrefixLength: Int = 5

/// A type representing a gRPC stream that is actively transferring data.
///
/// Constructed using `GRPCStreamPrototype`, and used to write to the network.
@objcMembers
public final class GRPCStream: NSObject {
  private let underlyingStream: Stream

  // MARK: - Internal

  /// Initialize a new instance of the stream.
  ///
  /// - parameter underlyingStream: The underlying stream to use for sending data.
  required init(underlyingStream: Stream) {
    self.underlyingStream = underlyingStream
    super.init()
  }

  // MARK: - Public

  /// Send headers over the gRPC stream.
  ///
  /// - parameter headers:   Headers to send over the stream.
  /// - parameter endStream: Whether this is a headers-only request.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func sendHeaders(_ headers: GRPCRequestHeaders, endStream: Bool) -> GRPCStream {
    self.underlyingStream.sendHeaders(headers, endStream: endStream)
    return self
  }

  /// Send a protobuf message's binary data over the gRPC stream.
  ///
  /// - parameter messageData: Binary data of a protobuf message to send.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func sendMessage(_ messageData: Data) -> GRPCStream {
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
    prefixData.append(Data(bytes: &length, count: MemoryLayout<UInt32>.size))

    // Send prefix data followed by message data
    self.underlyingStream.sendData(prefixData)
    self.underlyingStream.sendData(messageData)
    return self
  }

  /// Close this connection.
  public func close() {
    // The gRPC protocol requires the client stream to close with a DATA frame.
    // More information here:
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests
    self.underlyingStream.close(data: Data())
  }
}

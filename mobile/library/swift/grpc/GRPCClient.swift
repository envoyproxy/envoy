import Foundation

/// Client that supports sending and receiving gRPC traffic.
@objcMembers
public final class GRPCClient: NSObject {
  private let streamClient: StreamClient

  /// Create a new gRPC client instance.
  ///
  /// - parameter streamClient: The stream client to use for gRPC streams.
  public init(streamClient: StreamClient) {
    self.streamClient = streamClient
  }

  /// Create a new gRPC stream prototype which can be used to start streams.
  ///
  /// - returns: The new gRPC stream prototype.
  public func newGRPCStreamPrototype() -> GRPCStreamPrototype {
    let prototype = self.streamClient.newStreamPrototype()
    return GRPCStreamPrototype(underlyingStream: prototype)
  }
}

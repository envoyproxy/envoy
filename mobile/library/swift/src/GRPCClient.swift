import Foundation

/// Client that supports sending and receiving gRPC traffic.
@objcMembers
public final class GRPCClient: NSObject {
  private let httpClient: HTTPClient

  /// Create a new gRPC client instance.
  ///
  /// - parameter httpClient: The HTTP client to use for gRPC streams.
  public init(httpClient: HTTPClient) {
    self.httpClient = httpClient
  }

  /// Send a gRPC request with the provided handler.
  ///
  /// - parameter request: The outbound gRPC request. See `GRPCRequestBuilder` for creation.
  /// - parameter handler: Handler for receiving responses.
  ///
  /// - returns: An emitter that can be used for sending more traffic over the stream.
  public func send(_ request: Request, handler: GRPCResponseHandler) -> GRPCStreamEmitter {
    let emitter = self.httpClient.send(request, handler: handler.underlyingHandler)
    return GRPCStreamEmitter(emitter: emitter)
  }
}

import Foundation

/// Builder used for constructing instances of `GRPCRequestHeaders`.
@objcMembers
public final class GRPCRequestHeadersBuilder: HeadersBuilder {
  /// Initialize a new builder.
  ///
  /// - parameter scheme:    The URL scheme for the request (i.e., "https").
  /// - parameter authority: The URL authority for the request (i.e., "api.foo.com").
  /// - parameter path:      Path for the RPC (i.e., `/pb.api.v1.Foo/GetBar`).
  public convenience init(scheme: String = "https", authority: String, path: String) {
    self.init(headers: [
      ":authority": [authority],
      ":method": ["POST"],
      ":path": [path],
      ":scheme": [scheme],
      "content-type": ["application/grpc"],
    ])
  }

  /// Add a specific timeout for the gRPC request. This will be sent in the `grpc-timeout` header.
  ///
  /// - parameter timeoutMs: Timeout, in milliseconds.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addTimeoutMs(_ timeoutMs: UInt?) -> GRPCRequestHeadersBuilder {
    let headerName = "grpc-timeout"
    if let timeoutMs = timeoutMs {
      self.set(name: headerName, value: ["\(timeoutMs)m"])
    } else {
      self.remove(name: headerName)
    }

    return self
  }

  /// Build the request headers using the current builder.
  ///
  /// - returns: New instance of request headers.
  public func build() -> GRPCRequestHeaders {
    return GRPCRequestHeaders(container: self.container)
  }
}

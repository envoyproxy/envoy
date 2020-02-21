import Foundation

/// Builder used for creating new gRPC `Request` instances.
@objcMembers
public final class GRPCRequestBuilder: NSObject {
  private let underlyingBuilder: RequestBuilder

  /// Initialize a new builder.
  ///
  /// - parameter path:      Path of the RPC (i.e., `/pb.api.v1.Foo/GetBar`).
  /// - parameter authority: Authority to use for the RPC (i.e., `api.foo.com`).
  /// - parameter useHTTPS:  Whether to use HTTPS (or HTTP).
  public init(path: String, authority: String, useHTTPS: Bool = true) {
    self.underlyingBuilder = RequestBuilder(method: .post,
                                            scheme: useHTTPS ? "https" : "http",
                                            authority: authority,
                                            path: path)
    self.underlyingBuilder.addHeader(name: "content-type", value: "application/grpc")
    self.underlyingBuilder.addUpstreamHttpProtocol(.http2)
  }

  /// Append a value to the header key.
  ///
  /// - parameter name:  The header key.
  /// - parameter value: Value the value associated to the header key.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addHeader(name: String, value: String) -> GRPCRequestBuilder {
    self.underlyingBuilder.addHeader(name: name, value: value)
    return self
  }

  /// Remove all headers with this name.
  ///
  /// - parameter name: The header key to remove.
  ///
  /// - returns: This builder.
  @discardableResult
  public func removeHeaders(name: String) -> GRPCRequestBuilder {
    self.underlyingBuilder.removeHeaders(name: name)
    return self
  }

  /// Remove the value in the specified header.
  ///
  /// - parameter name: The header key to remove.
  /// - parameter value: The value to be removed.
  ///
  /// - returns: This builder.
  @discardableResult
  public func removeHeader(name: String, value: String) -> GRPCRequestBuilder {
    self.underlyingBuilder.removeHeader(name: name, value: value)
    return self
  }

  /// Add a specific timeout for the gRPC request. This will be sent in the `grpc-timeout` header.
  ///
  /// - parameter timeoutMS: Timeout, in milliseconds.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addTimeoutMS(_ timeoutMS: UInt?) -> GRPCRequestBuilder {
    let headerName = "grpc-timeout"
    if let timeoutMS = timeoutMS {
      self.addHeader(name: headerName, value: "\(timeoutMS)m")
    } else {
      self.removeHeaders(name: headerName)
    }

    return self
  }

  /// Creates a request object from the builder.
  ///
  /// - returns: The new request object.
  public func build() -> Request {
    return self.underlyingBuilder.build()
  }
}

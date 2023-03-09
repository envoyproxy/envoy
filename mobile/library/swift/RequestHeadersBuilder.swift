import Foundation

/// Builder used for constructing instances of `RequestHeaders`.
@objcMembers
public final class RequestHeadersBuilder: HeadersBuilder {
  /// Initialize a new instance of the builder.
  ///
  /// - parameter method:    Method for the request.
  /// - parameter scheme:    The URL scheme for the request (i.e., "https").
  /// - parameter authority: The URL authority for the request (i.e., "api.foo.com").
  /// - parameter path:      The URL path for the request (i.e., "/foo").
  public convenience init(method: RequestMethod, scheme: String = "https",
                          authority: String, path: String)
  {
    self.init(headers: [
      ":authority": [authority],
      ":method": [method.stringValue],
      ":path": [path],
      ":scheme": [scheme],
    ])
  }

  /// Add a retry policy to be used with this request.
  ///
  /// - parameter retryPolicy: The retry policy to use.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addRetryPolicy(_ retryPolicy: RetryPolicy) -> RequestHeadersBuilder {
    for (name, value) in retryPolicy.outboundHeaders() {
      self.internalSet(name: name, value: value)
    }

    return self
  }

#if ENVOY_MOBILE_REQUEST_COMPRESSION
  /// Compress this request's body using the specified algorithm.
  ///
  /// - note: Will only apply if the content length exceeds 30 bytes.
  ///
  /// - parameter algorithm: The compression algorithm to use to compress this request.
  ///
  /// - returns: This builder.
  @discardableResult
  public func enableRequestCompression(using algorithm: CompressionAlgorithm)
    -> RequestHeadersBuilder
  {
    self.internalSet(name: "x-envoy-mobile-compression", value: [algorithm.rawValue])
    return self
  }
#endif

  /// Build the request headers using the current builder.
  ///
  /// - returns: New instance of request headers.
  public func build() -> RequestHeaders {
    return RequestHeaders(container: self.container)
  }
}

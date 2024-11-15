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

  /// Build the request headers using the current builder.
  ///
  /// - returns: New instance of request headers.
  public func build() -> RequestHeaders {
    return RequestHeaders(container: self.container)
  }
}

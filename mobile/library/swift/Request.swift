import Foundation

/// Represents an Envoy HTTP request. Use `RequestBuilder` to construct new instances.
@objcMembers
public final class Request: NSObject {
  /// Method for the request.
  public let method: RequestMethod
  /// URL for the request.
  public let url: URL
  /// Headers to send with the request.
  /// Multiple values for a given name are valid, and will be sent as comma-separated values.
  public let headers: [String: [String]]
  /// Trailers to send with the request.
  /// Multiple values for a given name are valid, and will be sent as comma-separated values.
  public let trailers: [String: [String]]
  // Serialized data to send as the body of the request.
  public let body: Data?
  // Retry policy to use for this request.
  public let retryPolicy: RetryPolicy?

  /// Converts the request back to a builder so that it can be modified (i.e., by a filter).
  ///
  /// - returns: A new builder including all the properties of this request.
  public func newBuilder() -> RequestBuilder {
    return RequestBuilder(request: self)
  }

  /// Internal initializer called from the builder to create a new request.
  init(method: RequestMethod,
       url: URL,
       headers: [String: [String]] = [:],
       trailers: [String: [String]] = [:],
       body: Data?,
       retryPolicy: RetryPolicy?)
  {
    self.method = method
    self.url = url
    self.headers = headers
    self.trailers = trailers
    self.body = body
    self.retryPolicy = retryPolicy
  }
}

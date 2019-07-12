import Foundation

/// Builder used for constructing instances of `Request` types.
@objcMembers
public final class RequestBuilder: NSObject {
  /// Method for the request.
  public private(set) var method: RequestMethod
  /// URL for the request.
  public private(set) var url: URL
  /// Headers to send with the request.
  /// Multiple values for a given name are valid, and will be sent as comma-separated values.
  public private(set) var headers: [String: [String]] = [:]
  /// Trailers to send with the request.
  /// Multiple values for a given name are valid, and will be sent as comma-separated values.
  public private(set) var trailers: [String: [String]] = [:]
  // Serialized data to send as the body of the request.
  public private(set) var body: Data?
  // Retry policy to use for this request.
  public private(set) var retryPolicy: RetryPolicy?

  // MARK: - Initializers

  /// Internal initializer used for converting a request back to a builder.
  init(request: Request) {
    self.method = request.method
    self.url = request.url
    self.headers = request.headers
    self.trailers = request.trailers
    self.body = request.body
    self.retryPolicy = request.retryPolicy
  }

  /// Public initializer.
  public init(method: RequestMethod, url: URL) {
    self.method = method
    self.url = url
  }

  // MARK: - Builder functions

  @discardableResult
  public func addHeader(name: String, value: String) -> RequestBuilder {
    self.headers[name, default: []].append(value)
    return self
  }

  @discardableResult
  public func removeHeaders(name: String) -> RequestBuilder {
    self.headers.removeValue(forKey: name)
    return self
  }

  @discardableResult
  public func removeHeader(name: String, value: String) -> RequestBuilder {
    self.headers[name]?.removeAll(where: { $0 == value })
    if self.headers[name]?.isEmpty == true {
      self.headers.removeValue(forKey: name)
    }

    return self
  }

  @discardableResult
  public func addTrailer(name: String, value: String) -> RequestBuilder {
    self.trailers[name, default: []].append(value)
    return self
  }

  @discardableResult
  public func removeTrailer(name: String) -> RequestBuilder {
    self.trailers.removeValue(forKey: name)
    return self
  }

  @discardableResult
  public func removeTrailers(named name: String, value: String) -> RequestBuilder {
    self.trailers[name]?.removeAll(where: { $0 == value })
    if self.trailers[name]?.isEmpty == true {
      self.trailers.removeValue(forKey: name)
    }

    return self
  }

  @discardableResult
  public func addBody(_ body: Data?) -> RequestBuilder {
    self.body = body
    return self
  }

  @discardableResult
  public func addRetryPolicy(_ retryPolicy: RetryPolicy) -> RequestBuilder {
    self.retryPolicy = retryPolicy
    return self
  }

  public func build() -> Request {
    return Request(method: self.method,
                   url: self.url,
                   headers: self.headers,
                   trailers: self.trailers,
                   body: self.body,
                   retryPolicy: self.retryPolicy)
  }
}

// MARK: - Objective-C helpers

extension Request {
  /// Convenience builder function to allow for cleaner Objective-C syntax.
  ///
  /// For example:
  ///
  /// Request *req = [Request withMethod:RequestMethodGet url:url build:^(RequestBuilder *builder) {
  ///   [builder addBody:bodyData];
  ///   [builder addHeaderWithName:@"x-some-header" value:@"foo"];
  ///   [builder addTrailerWithName:@"x-some-trailer" value:@"foo"];
  /// }];
  @objc
  public static func with(method: RequestMethod, url: URL, build: (RequestBuilder) -> Void)
    -> Request
  {
    let builder = RequestBuilder(method: method, url: url)
    build(builder)
    return builder.build()
  }
}

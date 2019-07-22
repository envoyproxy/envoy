import Foundation

/// Builder used for constructing instances of `Request` types.
@objcMembers
public final class RequestBuilder: NSObject {
  /// Method for the request.
  public let method: RequestMethod
  /// The URL scheme for the request (i.e., "https").
  public let scheme: String
  /// The URL authority for the request (i.e., "api.foo.com").
  public let authority: String
  /// The URL path for the request (i.e., "/foo").
  public let path: String
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
    self.scheme = request.scheme
    self.authority = request.authority
    self.path = request.path
    self.headers = request.headers
    self.trailers = request.trailers
    self.body = request.body
    self.retryPolicy = request.retryPolicy
  }

  /// Public initializer.
  public init(method: RequestMethod, scheme: String, authority: String, path: String) {
    self.method = method
    self.scheme = scheme
    self.authority = authority
    self.path = path
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
  public func removeTrailers(name: String) -> RequestBuilder {
    self.trailers.removeValue(forKey: name)
    return self
  }

  @discardableResult
  public func removeTrailer(name: String, value: String) -> RequestBuilder {
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
                   scheme: self.scheme,
                   authority: self.authority,
                   path: self.path,
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
  /// Request *req = [Request withMethod:RequestMethodGet (...) build:^(RequestBuilder *builder) {
  ///   [builder addBody:bodyData];
  ///   [builder addHeaderWithName:@"x-some-header" value:@"foo"];
  ///   [builder addTrailerWithName:@"x-some-trailer" value:@"foo"];
  /// }];
  @objc
  public static func with(method: RequestMethod,
                          scheme: String,
                          authority: String,
                          path: String,
                          build: (RequestBuilder) -> Void)
    -> Request
  {
    let builder = RequestBuilder(method: method, scheme: scheme,
                                 authority: authority, path: path)
    build(builder)
    return builder.build()
  }
}

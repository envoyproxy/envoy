import Foundation

/// Headers representing an outbound request.
@objcMembers
public class RequestHeaders: Headers {
  /// Method for the request.
  public var method: RequestMethod! {
    return self.value(forName: ":method")?.first.flatMap(RequestMethod.init)
  }

  /// The URL scheme for the request (i.e., "https").
  public var scheme: String! {
    return self.value(forName: ":scheme")?.first
  }

  /// The URL authority for the request (i.e., "api.foo.com").
  public var authority: String! {
    return self.value(forName: ":authority")?.first
  }

  /// The URL path for the request (i.e., "/foo").
  public var path: String! {
    return self.value(forName: ":path")?.first
  }

  /// Retry policy to use for this request.
  public var retryPolicy: RetryPolicy? {
    return RetryPolicy.from(headers: self)
  }

  /// The protocol version to use for upstream requests.
  public var upstreamHttpProtocol: UpstreamHttpProtocol? {
    return self.value(forName: "x-envoy-mobile-upstream-protocol")?.first
      .flatMap(UpstreamHttpProtocol.init)
  }

  /// Convert the headers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toRequestHeadersBuilder() -> RequestHeadersBuilder {
    return RequestHeadersBuilder(headers: self.headers)
  }
}

// swiftlint:disable force_unwrapping - Programmer error if properties are nil when accessed
import Foundation

/// Headers representing an outbound request.
@objcMembers
public class RequestHeaders: Headers {
  /// Method for the request.
  public private(set) lazy var method: RequestMethod =
    (self.value(forName: ":method")?.first.flatMap(RequestMethod.init))!

  /// The URL scheme for the request (i.e., "https").
  public private(set) lazy var scheme: String = (self.value(forName: ":scheme")?.first)!

  /// The URL authority for the request (i.e., "api.foo.com").
  public private(set) lazy var authority: String = (self.value(forName: ":authority")?.first)!

  /// The URL path for the request (i.e., "/foo").
  public private(set) lazy var path: String = (self.value(forName: ":path")?.first)!

  /// Retry policy to use for this request.
  public private(set) lazy var retryPolicy: RetryPolicy? = .from(headers: self)

  /// The protocol version to use for upstream requests.
  public private(set) lazy var upstreamHttpProtocol: UpstreamHttpProtocol? =
    self.value(forName: "x-envoy-mobile-upstream-protocol")?.first
      .flatMap(UpstreamHttpProtocol.init)

  /// Convert the headers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toRequestHeadersBuilder() -> RequestHeadersBuilder {
    return RequestHeadersBuilder(container: self.container)
  }
}

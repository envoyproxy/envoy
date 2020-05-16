import Foundation

/// Builder used for constructing instances of `RequestTrailers`.
@objcMembers
public final class RequestTrailersBuilder: HeadersBuilder {
  /// Build the request trailers using the current builder.
  ///
  /// - returns: New instance of request trailers.
  public func build() -> RequestTrailers {
    return RequestTrailers(headers: self.headers)
  }
}

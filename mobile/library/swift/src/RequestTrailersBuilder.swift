import Foundation

/// Builder used for constructing instances of `RequestTrailers`.
@objcMembers
public final class RequestTrailersBuilder: HeadersBuilder {
  /// Initialize a new instance of the builder.
  public convenience init() {
    self.init(headers: [:])
  }

  /// Build the request trailers using the current builder.
  ///
  /// - returns: New instance of request trailers.
  public func build() -> RequestTrailers {
    return RequestTrailers(headers: self.headers)
  }
}

import Foundation

/// Trailers representing an inbound response.
@objcMembers
public final class ResponseTrailers: Headers {
  /// Convert the trailers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toResponseTrailersBuilder() -> ResponseTrailersBuilder {
    return ResponseTrailersBuilder(headers: self.headers)
  }
}

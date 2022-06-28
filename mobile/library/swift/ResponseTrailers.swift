import Foundation

/// Trailers representing an inbound response.
@objcMembers
public final class ResponseTrailers: Trailers {
  /// Convert the trailers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toResponseTrailersBuilder() -> ResponseTrailersBuilder {
    return ResponseTrailersBuilder(container: self.container)
  }
}

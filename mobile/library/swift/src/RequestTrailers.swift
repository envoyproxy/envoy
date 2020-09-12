import Foundation

/// Trailers representing an outbound request.
@objcMembers
public final class RequestTrailers: Trailers {
  /// Convert the trailers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toRequestTrailersBuilder() -> RequestTrailersBuilder {
    return RequestTrailersBuilder(headers: self.headers)
  }
}

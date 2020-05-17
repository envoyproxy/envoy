import Foundation

/// Builder used for constructing instances of `ResponseHeaders`.
@objcMembers
public final class ResponseHeadersBuilder: HeadersBuilder {
  /// Build the response headers using the current builder.
  ///
  /// - returns: New instance of response headers.
  public func build() -> ResponseHeaders {
    return ResponseHeaders(headers: self.headers)
  }
}

import Foundation

/// Builder used for constructing instances of `ResponseHeaders`.
@objcMembers
public final class ResponseHeadersBuilder: HeadersBuilder {
  /// Initialize a new instance of the builder.
  public override convenience init() {
    self.init(headers: [:])
  }

  /// Add an HTTP status to the response headers.
  ///
  /// - parameter status: The HTTP status to add.
  ///
  /// - returns: This builder.
  public func addHttpStatus(_ status: Int) -> ResponseHeadersBuilder {
    self.internalSet(name: ":status", value: ["\(status)"])
    return self
  }

  /// Build the response headers using the current builder.
  ///
  /// - returns: New instance of response headers.
  public func build() -> ResponseHeaders {
    return ResponseHeaders(headers: self.headers)
  }
}

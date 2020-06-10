import Foundation

/// Headers representing an inbound response.
@objcMembers
public final class ResponseHeaders: Headers {
  /// HTTP status code received with the response.
  public private(set) lazy var httpStatus: Int? =
    self.value(forName: ":status")?.first.flatMap(Int.init)

  /// Convert the headers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toResponseHeadersBuilder() -> ResponseHeadersBuilder {
    return ResponseHeadersBuilder(headers: self.headers)
  }
}

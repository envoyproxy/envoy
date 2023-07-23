import Foundation

/// Headers representing an inbound response.
@objcMembers
public final class ResponseHeaders: Headers {
  /// HTTP status code received with the response.
  public private(set) lazy var httpStatus: UInt? =
    self.value(forName: ":status")?.first.flatMap(UInt.init)

  /// Convert the headers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toResponseHeadersBuilder() -> ResponseHeadersBuilder {
    return ResponseHeadersBuilder(container: self.container)
  }
}

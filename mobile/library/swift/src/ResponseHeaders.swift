import Foundation

/// Headers representing an inbound response.
@objcMembers
public final class ResponseHeaders: Headers {
  /// HTTP status code received with the response.
  public var httpStatus: Int? {
    return self.value(forName: ":status")?.first.flatMap(Int.init)
  }
}

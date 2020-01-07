import Foundation

/// Error type containing information on failures reported by Envoy.
@objcMembers
public final class EnvoyError: NSObject, Error {
  /// Error code associated with the exception that occurred.
  public let errorCode: UInt64
  /// A description of what exception that occurred.
  public let message: String
  /// Optional cause for the error.
  public let cause: Error?

  public init(errorCode: UInt64, message: String, cause: Error?) {
    self.errorCode = errorCode
    self.message = message
    self.cause = cause
  }
}

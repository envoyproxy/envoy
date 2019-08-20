import Foundation

@objcMembers
public final class EnvoyError: NSObject, Error {
  /// Error code associated with the exception that occurred.
  public let errorCode: Int
  /// A description of what exception that occurred.
  public let message: String
  /// Optional cause for the error.
  public let cause: Error?

  init(errorCode: Int, message: String, cause: Swift.Error?) {
    self.errorCode = errorCode
    self.message = message
    self.cause = cause
  }
}

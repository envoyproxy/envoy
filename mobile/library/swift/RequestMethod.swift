import Foundation

/// Represents an HTTP request method.
@objc
public enum RequestMethod: Int {
  case delete
  case get
  case head
  case options
  case patch
  case post
  case put
  case trace

  /// String representation of this method.
  var stringValue: String {
    switch self {
    case .delete:
      return "DELETE"
    case .get:
      return "GET"
    case .head:
      return "HEAD"
    case .options:
      return "OPTIONS"
    case .patch:
      return "PATCH"
    case .post:
      return "POST"
    case .put:
      return "PUT"
    case .trace:
      return "TRACE"
    }
  }
}

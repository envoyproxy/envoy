import Foundation

/// Represents an HTTP request method.
@objc
public enum RequestMethod: Int, CaseIterable {
  case delete
  case get
  case head
  case options
  case patch
  case post
  case put
  case trace

  /// String representation of this method.
  public var stringValue: String {
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

  /// Initialize the method using a string value.
  ///
  /// - parameter stringValue: Case-insensitive string value to use for initialization.
  init(stringValue: String) {
    switch stringValue.uppercased() {
    case "DELETE":
      self = .delete
    case "GET":
      self = .get
    case "HEAD":
      self = .head
    case "OPTIONS":
      self = .options
    case "PATCH":
      self = .patch
    case "POST":
      self = .post
    case "PUT":
      self = .put
    case "TRACE":
      self = .trace
    default:
      fatalError("invalid value '\(stringValue)'")
    }
  }
}

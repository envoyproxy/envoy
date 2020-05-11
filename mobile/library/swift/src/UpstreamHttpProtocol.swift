import Foundation

/// Available upstream HTTP protocols.
@objc
public enum UpstreamHttpProtocol: Int, CaseIterable {
  case http1
  case http2

  /// String representation of the protocol.
  var stringValue: String {
    switch self {
    case .http1:
      return "http1"
    case .http2:
      return "http2"
    }
  }

  /// Initialize the protocol using a string value.
  ///
  /// - parameter stringValue: Case-insensitive string value to use for initialization.
  init?(stringValue: String) {
    switch stringValue.lowercased() {
    case "http1":
      self = .http1
    case "http2":
      self = .http2
    default:
      return nil
    }
  }
}

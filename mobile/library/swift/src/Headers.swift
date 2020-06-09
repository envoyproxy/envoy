import Foundation

/// Base class that is used to represent header/trailer data structures.
/// To instantiate new instances, see `{Request|Response}HeadersBuilder`.
@objcMembers
public class Headers: NSObject {
  let headers: [String: [String]]

  /// Get the value for the provided header name.
  ///
  /// - parameter name: Header name for which to get the current value.
  ///
  /// - returns: The current headers specified for the provided name.
  public func value(forName name: String) -> [String]? {
    return self.headers[name]
  }

  /// Internal initializer used by builders.
  ///
  /// - parameter headers: Headers to set.
  required init(headers: [String: [String]]) {
    self.headers = headers
    super.init()
  }
}

// MARK: - Equatable

extension Headers {
  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Self)?.headers == self.headers
  }
}

// MARK: - CustomStringConvertible

extension Headers {
  public override var description: String {
    return "\(type(of: self)) \(self.headers.description)"
  }
}

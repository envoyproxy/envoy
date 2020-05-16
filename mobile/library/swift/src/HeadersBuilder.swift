import Foundation

/// Base builder class used to construct `Headers` instances.
/// See `{Request|Response}HeadersBuilder` for usage.
@objcMembers
public class HeadersBuilder: NSObject {
  private(set) var headers: [String: [String]]

  /// Append a value to the header name.
  ///
  /// - parameter name:  The header name.
  /// - parameter value: Value the value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func add(name: String, value: String) -> Self {
    self.headers[name, default: []].append(value)
    return self
  }

  /// Replace all values at the provided name with a new set of header values.
  ///
  /// - parameter name: The header name.
  /// - parameter value: Value the value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func set(name: String, value: [String]) -> Self {
    self.headers[name] = value
    return self
  }

  /// Remove all headers with this name.
  ///
  /// - parameter name: The header name to remove.
  ///
  /// - returns: This builder.
  @discardableResult
  public func remove(name: String) -> Self {
    self.headers[name] = nil
    return self
  }

  /// Instantiate a new builder.
  ///
  /// - parameter headers: The headers to start with.
  init(headers: [String: [String]]) {
    self.headers = headers
  }
}

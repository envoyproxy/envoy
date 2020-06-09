import Foundation

private let kRestrictedPrefixes = [":", "x-envoy-mobile"]

private func isRestrictedHeader(name: String) -> Bool {
  return kRestrictedPrefixes.contains { name.hasPrefix($0) }
}

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
    if isRestrictedHeader(name: name) {
      return self
    }

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
    if isRestrictedHeader(name: name) {
      return self
    }

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
    if isRestrictedHeader(name: name) {
      return self
    }

    self.headers[name] = nil
    return self
  }

  // MARK: - Internal

  /// Allows for setting headers that are not publicly mutable (i.e., restricted headers).
  ///
  /// - parameter name: The header name.
  /// - parameter value: Value the value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  func internalSet(name: String, value: [String]) -> Self {
    self.headers[name] = value
    return self
  }

  /// Initialize a new builder. Subclasses should provide their own public convenience initializers.
  ///
  /// - parameter headers: The headers with which to start.
  required init(headers: [String: [String]]) {
    self.headers = headers
    super.init()
  }
}

// MARK: - Equatable

extension HeadersBuilder {
  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Self)?.headers == self.headers
  }
}

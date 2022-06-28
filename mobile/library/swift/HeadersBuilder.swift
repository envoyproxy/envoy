import Foundation

private let kRestrictedPrefixes = [":", "x-envoy-mobile"]

private func isRestrictedHeader(name: String) -> Bool {
  let isHostHeader = name.caseInsensitiveCompare("host") == .orderedSame
  lazy var hasRestrictedPrefix = kRestrictedPrefixes
    .contains { name.range(of: $0, options: [.caseInsensitive, .anchored]) != nil }
  return isHostHeader || hasRestrictedPrefix
}

/// Base builder class used to construct `Headers` instances.
/// It preserves the original casing of headers and enforces
/// a case-insensitive lookup and setting of headers.
/// See `{Request|Response}HeadersBuilder` for usage.
@objcMembers
public class HeadersBuilder: NSObject {
  private(set) var container: HeadersContainer

  /// Append a value to the header name.
  ///
  /// - parameter name:  The header name.
  /// - parameter value: The value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func add(name: String, value: String) -> Self {
    if isRestrictedHeader(name: name) {
      return self
    }

    self.container.add(name: name, value: value)
    return self
  }

  /// Replace all values at the provided name with a new set of header values.
  ///
  /// - parameter name:  The header name.
  /// - parameter value: The value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func set(name: String, value: [String]) -> Self {
    if isRestrictedHeader(name: name) {
      return self
    }

    self.container.set(name: name, value: value)
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

    self.container.set(name: name, value: nil)
    return self
  }

  // MARK: - Internal

  /// Allows for setting headers that are not publicly mutable (i.e., restricted headers).
  ///
  /// - parameter name:  The header name.
  /// - parameter value: The value associated to the header name.
  ///
  /// - returns: This builder.
  @discardableResult
  func internalSet(name: String, value: [String]) -> Self {
    self.container.set(name: name, value: value)
    return self
  }

  func allHeaders() -> [String: [String]] {
    return self.container.allHeaders()
  }

  // Only explicitly implemented to work around a swiftinterface issue in Swift 5.1. This can be
  // removed once envoy is only built with Swift 5.2+
  public override init() {
    self.container = HeadersContainer()
    super.init()
  }

  // Initialize a new builder using the provided headers container.
  ///
  /// - parameter container: The headers container to initialize the receiver with.
  init(container: HeadersContainer) {
    self.container = container
    super.init()
  }

  // Initialize a new builder. Subclasses should provide their own public convenience initializers.
  //
  // - parameter headers: The headers with which to start.
  init(headers: [String: [String]]) {
    self.container = HeadersContainer(headers: headers)
    super.init()
  }
}

// MARK: - Equatable

extension HeadersBuilder {
  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Self)?.container == self.container
  }
}

import Foundation

/// Builder class used to construct `Tags` instances.
/// See `TagsBuilder` for usage.
@objcMembers
public class TagsBuilder: NSObject {
  private(set) var tags: [String: String]

  /// Append a value to the tag name.
  ///
  /// - parameter name:  The tag name.
  /// - parameter value: The value associated to the tag name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func add(name: String, value: String) -> Self {
    self.tags[name] = value
    return self
  }

  /// Replace all values at the provided name with a new set of tag values.
  ///
  /// - parameter name: The tag name.
  /// - parameter value: The value associated to the tag name.
  ///
  /// - returns: This builder.
  @discardableResult
  public func set(name: String, value: String) -> Self {
    self.tags[name] = value
    return self
  }

  /// Remove all tags with this name.
  ///
  /// - parameter name: The tag name to remove.
  ///
  /// - returns: This builder.
  @discardableResult
  public func remove(name: String) -> Self {
    self.tags[name] = nil
    return self
  }

  /// Add all tags from dictionary to builder.
  ///
  /// - parameter name: a dictionary of tags.
  ///
  /// - returns: This builder.
  @discardableResult
  public func putAll(tags: [String: String]) -> Self {
    self.tags = self.tags.merging(tags, uniquingKeysWith: { _, last in last })
    return self
  }

  /// Build the tags using the current builder.
  ///
  /// - returns: New instance of request headers.
  public func build() -> Tags {
    return Tags(tags: self.tags)
  }

  // MARK: - Internal

  /// Allows for setting tags that are not publicly mutable (i.e., restricted tags).
  ///
  /// - parameter name: The tag name.
  /// - parameter value: The value associated to the tag name.
  ///
  /// - returns: This builder.
  @discardableResult
  func internalSet(name: String, value: String) -> Self {
    self.tags[name] = value
    return self
  }

  // Only explicitly implemented to work around a swiftinterface issue in Swift 5.1. This can be
  // removed once envoy is only built with Swift 5.2+
  override required init() {
    self.tags = [String: String]()
    super.init()
  }

  /// Initialize a new builder. Subclasses should provide their own public convenience initializers.
  ///
  /// - parameter tags: The tags with which to start.
  required init(tags: [String: String]) {
    self.tags = tags
    super.init()
  }
}

// MARK: - Equatable

extension TagsBuilder {
  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Self)?.tags == self.tags
  }
}

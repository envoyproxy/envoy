import Foundation

/// Class used to represent tags data structures.
/// To instantiate new instances, see `TagsBuilder`.
@objcMembers
public class Tags: NSObject {
  let tags: [String: String]

  /// Get the value for the provided tag name.
  ///
  /// - parameter name: tag name for which to get the current value.
  ///
  /// - returns: The current tag value specified for the provided name.
  public func value(forName name: String) -> String? {
    return self.tags[name]
  }

  /// Accessor for all underlying tags as a map.
  ///
  /// - returns: The underlying tags.
  public func allTags() -> [String: String] {
    return self.tags
  }

  /// Internal initializer used by builders.
  ///
  /// - parameter tags: Tags to set.
  required init(tags: [String: String]) {
    self.tags = tags
    super.init()
  }

  /// Internal initializer used by builders.
  override required init() {
    self.tags = [String: String]()
    super.init()
  }
}

// MARK: - Equatable

extension Tags {
  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Self)?.tags == self.tags
  }
}

// MARK: - CustomStringConvertible

extension Tags {
  public override var description: String {
    return "\(type(of: self)) \(self.tags.description)"
  }
}

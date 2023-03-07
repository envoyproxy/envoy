import Foundation

private let kPattern = "^[A-Za-z_]+$"

/// Element represents one dot-delimited component of a time series name.
/// Element values must conform to the regex /^[A-Za-z_]+$/.
@objcMembers
public final class Element: NSObject, ExpressibleByStringLiteral {
  internal let value: String

  public init(stringLiteral value: String) {
    guard value.range(of: kPattern, options: .regularExpression) != nil else {
      preconditionFailure("Element values must conform to the regex '\(kPattern)'.")
    }
    self.value = value
  }

  public override func isEqual(_ object: Any?) -> Bool {
    return (object as? Element)?.value == self.value
  }
}

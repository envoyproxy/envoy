@_implementationOnly import EnvoyEngine
import Foundation

/// A time series counter.
@objcMembers
public class Counter: NSObject {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Increment the counter by the given count.
  public func increment(count: Int = 1) {
    guard let engine = self.engine else {
      return
    }

    engine.recordCounter(self.series, count: numericCast(count))
  }
}

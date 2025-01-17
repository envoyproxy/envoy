@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of time series counter.
@objcMembers
final class CounterImpl: NSObject, Counter {
  private let series: String
  private let tags: Tags
  private weak var engine: EnvoyEngine?

  init(elements: [Element], tags: Tags, engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    self.tags = tags
    super.init()
  }

  /// Increment the counter by the given count.
  ///
  /// - parameter count: The count to increment on this counter.
  func increment(count: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordCounterInc(self.series, tags: self.tags.allTags(), count: numericCast(count))
  }

  /// Increment the counter by the given count with tags.
  ///
  /// - parameter tags:  The tags to attach to this counter.
  /// - parameter count: The count to increment on this counter.
  func increment(tags: Tags, count: Int) {
    self.engine?.recordCounterInc(self.series, tags: tags.allTags(), count: numericCast(count))
  }
}

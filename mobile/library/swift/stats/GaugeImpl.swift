@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of time series gauge.
@objcMembers
final class GaugeImpl: NSObject, Gauge {
  private let series: String
  private let tags: Tags
  private weak var engine: EnvoyEngine?

  init(elements: [Element], tags: Tags, engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    self.tags = tags
    super.init()
  }

  /// Set the gauge with the given value.
  func set(value: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordGaugeSet(self.series, tags: self.tags.allTags(), value: numericCast(value))
  }

    /// Set the gauge with the given value and with tags
  func set(tags: Tags, value: Int) {
    self.engine?.recordGaugeSet(self.series, tags: tags.allTags(), value: numericCast(value))
  }

  /// Add the given amount to the gauge.
  func add(amount: Int) {
    self.engine?.recordGaugeAdd(self.series, tags: self.tags.allTags(), amount: numericCast(amount))
  }

  /// Add the given amount to the gauge with the given tags.
  func add(tags: Tags, amount: Int) {
    self.engine?.recordGaugeAdd(self.series, tags: tags.allTags(), amount: numericCast(amount))
  }

  /// Subtract the given amount from the gauge.
  func sub(amount: Int) {
    self.engine?.recordGaugeSub(self.series, tags: self.tags.allTags(), amount: numericCast(amount))
  }

  /// Subtract the given amount from the gauge with the given tags.
  func sub(tags: Tags, amount: Int) {
    self.engine?.recordGaugeSub(self.series, tags: tags.allTags(), amount: numericCast(amount))
  }
}

@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of time series gauge.
@objcMembers
final class GaugeImpl: NSObject, Gauge {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Set the gauge with the given value.
  func set(value: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordGaugeSet(self.series, value: numericCast(value))
  }

  /// Add the given amount to the gauge.
  func add(amount: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordGaugeAdd(self.series, amount: numericCast(amount))
  }

  /// Subtract the given amount from the gauge.
  func sub(amount: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordGaugeSub(self.series, amount: numericCast(amount))
  }
}

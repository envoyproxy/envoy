@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of time series gauge.
@objcMembers
class GaugeImpl: NSObject, Gauge {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Set the gauge with the given value.
  /// TODO: potentially raise error to platform if the operation is not successful.
  func set(value: Int) {
    guard let engine = self.engine else {
      return
    }

    engine.recordGaugeSet(self.series, value: numericCast(value))
  }

  /// Add the given amount to the gauge.
  /// TODO: potentially raise error to platform if the operation is not successful.
  func add(amount: Int) {
    guard let engine = self.engine else {
      return
    }

    engine.recordGaugeAdd(self.series, amount: numericCast(amount))
  }

  /// Subtract the given amount from the gauge.
  /// TODO: potentially raise error to platform if the operation is not successful.
  func sub(amount: Int) {
    guard let engine = self.engine else {
      return
    }

    engine.recordGaugeSub(self.series, amount: numericCast(amount))
  }
}

@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of distribution tracking quantile/sum/average stats
@objcMembers
class DistributionImpl: NSObject, Distribution {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Record a new int value for the distribution.
  /// TODO: potentially raise error to platform if the operation is not successful.
  func recordValue(value: Int) {
    guard let engine = self.engine else {
      return
    }

    engine.recordHistogramValue(self.series, value: numericCast(value))
  }
}

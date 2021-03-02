@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of distribution tracking quantile/sum/average stats
@objcMembers
final class DistributionImpl: NSObject, Distribution {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Record a new int value for the distribution.
  func recordValue(value: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordHistogramValue(self.series, value: numericCast(value))
  }
}

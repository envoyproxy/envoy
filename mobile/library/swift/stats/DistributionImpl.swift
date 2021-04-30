@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of distribution tracking quantile/sum/average stats
@objcMembers
final class DistributionImpl: NSObject, Distribution {
  private let series: String
  private let tags: Tags
  private weak var engine: EnvoyEngine?

  init(elements: [Element], tags: Tags, engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    self.tags = tags
    super.init()
  }

  /// Record a new int value for the distribution.
  func recordValue(value: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordHistogramValue(
      self.series, tags: self.tags.allTags(), value: numericCast(value))
  }

  /// Record a new int value for the distribution with tags.
  func recordValue(tags: Tags, value: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordHistogramValue(self.series, tags: tags.allTags(), value: numericCast(value))
  }
}

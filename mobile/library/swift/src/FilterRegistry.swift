/// Contains a registry of filter factories and may be used for creating filter chains
/// to be used with outbound requests/streams.
public final class FilterRegistry {
  private var factories = [() -> Filter]()

  /// Register a new filter factory that will be called to instantiate new filter instances for
  /// outbound requests/streams.
  ///
  /// - parameter factory: Closure that, when called, will return a new instance of a filter.
  ///                      The filter may be a `RequestFilter`, `ResponseFilter`, or both.
  public func register(factory: @escaping () -> Filter) {
    self.factories.append(factory)
  }

  func createChain() -> (requestChain: [RequestFilter], responseChain: [ResponseFilter]) {
    // TODO(rebello95): Finish implementing this function, linking up callbacks, and adding docs.
    let filters = self.factories.map { $0() }
    return (filters.compactMap { $0 as? RequestFilter },
            filters.reversed().compactMap { $0 as? ResponseFilter })
  }
}

package io.envoyproxy.envoymobile

/*
 * Contains a registry of filter factories and may be used for creating filter chains
 * to be used with outbound requests/streams.
 */
//  TODO: bidirectional filter.
//  TODO: explore callback pattern, i.e should we have setFilterCallbacks? Or pass the callback to
//        the filter constructor. In the past a filter was another filter's callback target, this
//        is not the case anymore, because we will have a filter manager which handles filter
//        iteration.
class FilterRegistry {
  private val factories = mutableListOf<() -> Filter>()

  /**
   * Register a new filter factory that will be called to instantiate new filter instances for
   * outbound requests/streams.
   *
   * @param factory: Closure that, when called, will return a new instance of a filter.
   *                 The filter may be a `RequestFilter`, `ResponseFilter`, or both.
   */
  fun register(factory: () -> Filter) {
    factories.add(factory)
  }

  private fun createChain(): Pair<List<RequestFilter>, List<ResponseFilter>> {
    // TODO: Finish implementing this function, linking up callbacks, and adding docs.
    val filters = factories.map { factory -> factory() }
    return Pair(
      filters.filterIsInstance<RequestFilter>(),
      filters.reversed().filterIsInstance<ResponseFilter>()
    )
  }
}

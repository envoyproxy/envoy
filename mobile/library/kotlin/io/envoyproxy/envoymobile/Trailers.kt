package io.envoyproxy.envoymobile

/*
 * Base class representing trailers data structures.
 * To instantiate new instances see `{Request|Response}TrailersBuilder`.
 */
open class Trailers : Headers {
  /**
   * Internal constructor used by builders.
   *
   * @param trailers: Trailers to set.
   */
  protected constructor(trailers: Map<String, List<String>>)
    : super(HeadersContainer.create(trailers))

  protected constructor(container: HeadersContainer) : super(container)
}

package io.envoyproxy.envoymobile

/** Trailers representing an outbound request. */
@Suppress("EmptyClassBlock")
class RequestTrailers : Trailers {
  /**
   * Internal constructor used by builders.
   *
   * @param trailers: Trailers to set.
   */
  internal constructor(trailers: Map<String, List<String>>) : super(trailers)

  /**
   * Instantiate a new builder.
   *
   * @param container: The headers container to start with.
   */
  internal constructor(container: HeadersContainer) : super(container)

  /**
   * Convert the trailers back to a builder for mutation.
   *
   * @return RequestTrailersBuilder, The new builder.
   */
  fun toRequestTrailersBuilder() = RequestTrailersBuilder(container)
}

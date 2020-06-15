package io.envoyproxy.envoymobile

/*
 * Trailers representing an inbound response.
 */
@Suppress("EmptyClassBlock")
class ResponseTrailers : Headers {
  /**
   * Internal constructor used by builders.
   *
   * @param trailers: Trailers to set.
   */
  internal constructor(trailers: Map<String, List<String>>) : super(trailers)
}

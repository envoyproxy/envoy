package io.envoyproxy.envoymobile

/*
 * Trailers representing an outbound request.
 */
@Suppress("EmptyClassBlock")
class RequestTrailers : Headers {
  /**
   * Internal constructor used by builders.
   *
   * @param trailers: Headers to set.
   */
  internal constructor(trailers: Map<String, List<String>>) : super(trailers)
}

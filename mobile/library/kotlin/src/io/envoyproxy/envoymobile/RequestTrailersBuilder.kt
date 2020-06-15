package io.envoyproxy.envoymobile

/*
 * Builder used for constructing instances of `RequestTrailers`.
 */
class RequestTrailersBuilder : HeadersBuilder {
  /**
   * Initialize a new instance of the builder.
   */
  constructor() : super(mutableMapOf())

  /**
   * Instantiate a new builder. Used only by RequestTrailers to convert back to
   * RequestTrailersBuilder.
   *
   * @param trailers: The trailers to start with.
   */
  internal constructor(trailers: MutableMap<String, MutableList<String>>) : super(trailers)

  /**
   * Build the request trailers using the current builder.
   *
   * @return RequestTrailers, New instance of request trailers.
   */
  fun build(): RequestTrailers {
    return RequestTrailers(headers)
  }
}

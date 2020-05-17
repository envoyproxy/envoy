package io.envoyproxy.envoymobile

/*
 * Builder used for constructing instances of `ResponseTrailers`.
 */
class ResponseTrailersBuilder: HeadersBuilder {
  /**
   * Initialize a new instance of the builder.
   */
  constructor() : super(mutableMapOf())

  /**
   * Instantiate a new builder. Used only by ResponseTrailers to convert back to
   * ResponseTrailersBuilder.
   *
   * @param trailers: The trailers to start with.
   */
  internal constructor(trailers: MutableMap<String, MutableList<String>>) : super(trailers)

  /**
   * Build the response trailers using the current builder.
   *
   * @return ResponseTrailers, New instance of response trailers.
   */
  fun build(): ResponseTrailers {
    return ResponseTrailers(headers)
  }
}

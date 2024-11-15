package io.envoyproxy.envoymobile

/** Builder used for constructing instances of `ResponseTrailers`. */
class ResponseTrailersBuilder : HeadersBuilder {
  /** Initialize a new instance of the builder. */
  constructor() : super(HeadersContainer(mapOf()))

  /**
   * Instantiate a new builder. Used only by ResponseTrailers to convert back to
   * ResponseTrailersBuilder.
   *
   * @param trailers: The trailers to start with.
   */
  internal constructor(
    trailers: MutableMap<String, MutableList<String>>
  ) : super(HeadersContainer(trailers))

  internal constructor(container: HeadersContainer) : super(container)

  override fun add(name: String, value: String): ResponseTrailersBuilder {
    super.add(name, value)
    return this
  }

  override fun set(name: String, value: MutableList<String>): ResponseTrailersBuilder {
    super.set(name, value)
    return this
  }

  override fun remove(name: String): ResponseTrailersBuilder {
    super.remove(name)
    return this
  }

  override fun internalSet(name: String, value: MutableList<String>): ResponseTrailersBuilder {
    super.internalSet(name, value)
    return this
  }

  /**
   * Build the response trailers using the current builder.
   *
   * @return ResponseTrailers, New instance of response trailers.
   */
  fun build(): ResponseTrailers {
    return ResponseTrailers(container)
  }
}

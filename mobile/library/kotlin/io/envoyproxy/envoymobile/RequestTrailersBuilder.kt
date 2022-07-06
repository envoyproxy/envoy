package io.envoyproxy.envoymobile

/**
 * Builder used for constructing instances of `RequestTrailers`.
 */
class RequestTrailersBuilder : HeadersBuilder {
  /*
   * Instantiate a new builder.
   */
  internal constructor() : super(HeadersContainer(mapOf()))

  /*
   * Instantiate a new instance of the builder.
   *
   * @param container: The headers container to start with.
   */
  internal constructor(container: HeadersContainer) : super(container)

  /*
   * Instantiate a new builder. Used only by RequestTrailers to convert back to
   * RequestTrailersBuilder.
   *
   * @param trailers: The trailers to start with.
   */
  internal constructor(trailers: MutableMap<String, MutableList<String>>)
    : super(HeadersContainer(trailers))

  override fun add(name: String, value: String): RequestTrailersBuilder {
    super.add(name, value)
    return this
  }

  override fun set(name: String, value: MutableList<String>): RequestTrailersBuilder {
    super.set(name, value)
    return this
  }

  override fun remove(name: String): RequestTrailersBuilder {
    super.remove(name)
    return this
  }

  override fun internalSet(name: String, value: MutableList<String>): RequestTrailersBuilder {
    super.internalSet(name, value)
    return this
  }

  /**
   * Build the request trailers using the current builder.
   *
   * @return RequestTrailers, New instance of request trailers.
   */
  fun build(): RequestTrailers {
    return RequestTrailers(container)
  }
}

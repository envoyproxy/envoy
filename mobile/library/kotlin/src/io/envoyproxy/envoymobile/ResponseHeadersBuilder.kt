package io.envoyproxy.envoymobile

/*
 * Builder used for constructing instances of `ResponseHeaders`.
 */
class ResponseHeadersBuilder : HeadersBuilder {

  /**
   * Initialize a new instance of the builder.
   */
  constructor() : super(mutableMapOf())

  /**
   * Instantiate a new builder. Used only by ResponseHeaders to convert back to
   * ResponseHeadersBuilder.
   *
   * @param headers: The headers to start with.
   */
  internal constructor(headers: MutableMap<String, MutableList<String>>) : super(headers)

  override fun add(name: String, value: String): ResponseHeadersBuilder {
    super.add(name, value)
    return this
  }

  override fun set(name: String, value: MutableList<String>): ResponseHeadersBuilder {
    super.set(name, value)
    return this
  }

  override fun remove(name: String): ResponseHeadersBuilder {
    super.remove(name)
    return this
  }

  override fun internalSet(name: String, value: MutableList<String>): ResponseHeadersBuilder {
    super.internalSet(name, value)
    return this
  }

  /**
   * Add an HTTP status to the response headers.
   *
   * @param status: The HTTP status to add.
   *
   * @return ResponseHeadersBuilder, This builder.
   */
  fun addHttpStatus(status: Int): ResponseHeadersBuilder {
    internalSet(":status", mutableListOf("$status"))
    return this
  }

  /**
   * Build the response headers using the current builder.
   *
   * @return ResponseHeaders, New instance of response headers.
   */
  fun build(): ResponseHeaders {
    return ResponseHeaders(headers)
  }
}

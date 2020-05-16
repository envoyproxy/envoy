package io.envoyproxy.envoymobile

/*
 * Base builder class used to construct `Headers` instances.
 * See `{Request|Response}HeadersBuilder` for usage.
 */
open class HeadersBuilder {
  protected val headers: MutableMap<String, MutableList<String>>

  /**
   * Instantiate a new builder, only used by child classes.
   *
   * @param headers: The headers to start with.
   */
  protected constructor(headers: MutableMap<String, MutableList<String>>) {
    this.headers = headers
  }

  /**
   * Append a value to the header name.
   *
   * @param name:  The header name.
   * @param value: Value the value associated to the header name.
   *
   * @return HeadersBuilder, This builder.
   */
  fun add(name: String, value: String): HeadersBuilder {
    headers.getOrPut(name) { mutableListOf<String>() }.add(value)
    return this
  }

  /**
   * Replace all values at the provided name with a new set of header values.
   *
   * @param name: The header name.
   * @param value: Value the value associated to the header name.
   *
   * @return HeadersBuilder, This builder.
   */
  fun set(name: String, value: MutableList<String>): HeadersBuilder {
    headers[name] = value
    return this
  }

  /**
   * Remove all headers with this name.
   *
   * @param name: The header name to remove.
   *
   * @return HeadersBuilder, This builder.
   */
  fun remove(name: String): HeadersBuilder {
    headers.remove(name)
    return this
  }
}

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
   * @param value: The value associated to the header name.
   *
   * @return HeadersBuilder, This builder.
   */
  open fun add(name: String, value: String): HeadersBuilder {
    if (isRestrictedHeader(name)) {
      return this
    }
    headers.getOrPut(name) { mutableListOf<String>() }.add(value)
    return this
  }

  /**
   * Replace all values at the provided name with a new set of header values.
   *
   * @param name: The header name.
   * @param value: The value associated to the header name.
   *
   * @return HeadersBuilder, This builder.
   */
  open fun set(name: String, value: MutableList<String>): HeadersBuilder {
    if (isRestrictedHeader(name)) {
      return this
    }
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
  open fun remove(name: String): HeadersBuilder {
    if (isRestrictedHeader(name)) {
      return this
    }
    headers.remove(name)
    return this
  }

  /**
   * Allows for setting headers that are not publicly mutable (i.e., restricted headers).
   *
   * @param name: The header name.
   * @param value: The value associated to the header name.
   *
   * @return HeadersBuilder, This builder.
   */
  internal open fun internalSet(name: String, value: MutableList<String>): HeadersBuilder {
    headers[name] = value
    return this
  }

  private fun isRestrictedHeader(name: String) = name.startsWith(":") ||
    name.startsWith("x-envoy-mobile") || name == "host"
}

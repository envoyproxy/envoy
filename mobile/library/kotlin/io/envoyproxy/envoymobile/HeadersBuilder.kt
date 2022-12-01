package io.envoyproxy.envoymobile

/**
 * Base builder class used to construct `Headers` instances.
 * See `{Request|Response}HeadersBuilder` for usage.
 */
open class HeadersBuilder {
  protected val container: HeadersContainer

  /**
   * Instantiate a new builder, only used by child classes.
   *
   * @param container: The headers container to start with.
   */
  internal constructor(container: HeadersContainer) {
    this.container = container
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
    container.add(name, value)
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
    container.set(name, value)
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
    container.remove(name)
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
    container.set(name, value)
    return this
  }

  private fun isRestrictedHeader(name: String) = name.startsWith(":") ||
    name.startsWith("x-envoy-mobile", ignoreCase = true) || name.equals("host", ignoreCase = true)
}

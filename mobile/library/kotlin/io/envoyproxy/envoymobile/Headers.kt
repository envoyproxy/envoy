package io.envoyproxy.envoymobile

/**
 * Base class that is used to represent header/trailer data structures.
 * To instantiate new instances, see `{Request|Response}HeadersBuilder`.
 */
open class Headers {
  internal val container: HeadersContainer

  /**
   * Internal constructor used by builders.
   *
   * @param container: The headers container to set.
   */
  internal constructor(container: HeadersContainer) {
    this.container = container
  }

  /**
   * Get the value for the provided header name. It's discouraged
   * to use this dictionary for equality key-based lookups as this
   * may lead to issues with headers that do not follow expected
   * casing i.e., "Content-Length" instead of "content-length".
   *
   * @param name: Header name for which to get the current value.
   *
   * @return The current headers specified for the provided name.
   */
  fun value(name: String): List<String>? {
    return container.value(name)
  }

  /**
   * Accessor for all underlying headers as a map.
   *
   * @return The underlying headers.
   */
  fun caseSensitiveHeaders(): Map<String, List<String>> {
    return container.caseSensitiveHeaders()
  }
}

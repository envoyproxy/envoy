package io.envoyproxy.envoymobile

/*
 * Base class that is used to represent header/trailer data structures.
 * To instantiate new instances, see `{Request|Response}HeadersBuilder`.
 */
open class Headers {
  @Suppress("MemberNameEqualsClassName")
  protected val headers: Map<String, List<String>>

  /**
   * Internal constructor used by builders.
   *
   * @param headers: Headers to set.
   */
  protected constructor(headers: Map<String, List<String>>) {
    this.headers = headers
  }

  /**
   * Get the value for the provided header name.
   *
   * @param name: Header name for which to get the current value.
   *
   * @return List<String>?, The current headers specified for the provided name.
   */
  fun value(name: String): List<String>? {
    return headers[name]
  }
}

package io.envoyproxy.envoymobile

/**
 * The container that manages the underlying headers map. It maintains the original casing of passed
 * header names. It treats headers names as case-insensitive for the purpose of header lookups and
 * header name conflict resolutions.
 */
open class HeadersContainer {
  protected val headers: MutableMap<String, Header>

  /**
   * Represents a header name together with all of its values. It preserves the original casing of
   * the header name.
   *
   * @param name The name of the header. Its casing is preserved.
   * @param value The value associated with a given header.
   */
  data class Header(val name: String, var value: MutableList<String>) {
    constructor(name: String) : this(name, mutableListOf())

    /**
     * Add values.
     *
     * @param values The list of values to add.
     */
    fun add(values: List<String>) {
      this.value.addAll(values)
    }

    /**
     * Add a value.
     *
     * @param value The value to add.
     */
    fun add(value: String) {
      this.value.add(value)
    }
  }

  /**
   * Instantiate a new instance of the receiver using the provided headers map
   *
   * @param headers The headers to start with.
   */
  internal constructor(headers: Map<String, MutableList<String>>) {
    var underlyingHeaders = mutableMapOf<String, Header>()
    /**
     * Dictionaries are unordered collections. Process headers with names that are the same when
     * lowercased in an alphabetical order to avoid a situation in which the result of the
     * initialization is non-derministic i.e., we want mapOf("A" to listOf("1"), "a" to listOf("2"))
     * headers to be always converted to mapOf("A" to listOf("1", "2")) and never to mapOf("a" to
     * listOf("2", "1")).
     *
     * If a given header name already exists in the processed headers map, check if the currently
     * processed header name is before the existing header name as determined by an alphabetical
     * order.
     */
    headers.forEach {
      val lowercased = it.key.lowercase()
      val existing = underlyingHeaders[lowercased]

      if (existing == null) {
        underlyingHeaders[lowercased] = Header(it.key, it.value)
      } else if (existing.name > it.key) {
        underlyingHeaders[lowercased] = Header(it.key, (it.value + existing.value).toMutableList())
      } else {
        underlyingHeaders[lowercased]?.add(it.value)
      }
    }

    this.headers = underlyingHeaders
  }

  companion object {
    /**
     * Create a new instance of the receiver using a provider headers map. Not implemented as a
     * constructor due to conflicting JVM signatures with other constructors.
     *
     * @param headers The headers to create the container with.
     */
    fun create(headers: Map<String, List<String>>): HeadersContainer {
      return HeadersContainer(headers.mapValues { it.value.toMutableList() })
    }
  }

  /**
   * Add a value to a header with a given name.
   *
   * @param name The name of the header. For the purpose of headers lookup and header name conflict
   *   resolution, the name of the header is considered to be case-insensitive.
   * @param value The value to add.
   */
  fun add(name: String, value: String) {
    val lowercased = name.lowercase()
    headers[lowercased]?.let { it.add(value) }
      ?: run { headers.put(lowercased, Header(name, mutableListOf(value))) }
  }

  /**
   * Set the value of a given header.
   *
   * @param name The name of the header.
   * @param value The value to set the header value to.
   */
  fun set(name: String, value: List<String>) {
    headers[name.lowercase()] = Header(name, value.toMutableList())
  }

  /**
   * Remove a given header.
   *
   * @param name The name of the header to remove.
   */
  fun remove(name: String) {
    headers.remove(name.lowercase())
  }

  /**
   * Get the value for the provided header name.
   *
   * @param name The case-insensitive header name for which to get the current value.
   * @return The value associated with a given header.
   */
  fun value(name: String): List<String>? {
    return headers[name.lowercase()]?.value
  }

  /**
   * Accessor for all underlying case-sensitive headers. When possible, use case-insensitive
   * accessors instead.
   *
   * @return The underlying headers.
   */
  fun caseSensitiveHeaders(): Map<String, List<String>> {
    var caseSensitiveHeaders = mutableMapOf<String, List<String>>()
    headers.forEach { caseSensitiveHeaders.put(it.value.name, it.value.value) }

    return caseSensitiveHeaders
  }
}

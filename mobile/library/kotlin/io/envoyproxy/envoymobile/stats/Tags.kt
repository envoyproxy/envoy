package io.envoyproxy.envoymobile

/*
 * Base class that is used to represent tags structures.
 * To instantiate new instances, see `TagsBuilder`.
 */
class Tags {
  @Suppress("MemberNameEqualsClassName") val tags: Map<String, String>

  /**
   * Internal constructor used by builders.
   *
   * @param tags: tags to set.
   */
  internal constructor(tags: Map<String, String>) {
    this.tags = tags
  }

  /**
   * Get the value for the provided tag name.
   *
   * @param name: Tag name for which to get the current value.
   * @return String?, The current tags specified for the provided name.
   */
  fun value(name: String): String? {
    return tags[name]
  }

  /**
   * Accessor for all underlying tags as a map.
   *
   * @return Map<String, String>, The underlying tags.
   */
  fun allTags(): Map<String, String> {
    return tags
  }
}

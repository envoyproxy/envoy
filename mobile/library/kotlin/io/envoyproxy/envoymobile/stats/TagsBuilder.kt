package io.envoyproxy.envoymobile

/*
 * Builder class used to construct `Tags` instances.
 */
public class TagsBuilder {
  protected val tags: MutableMap<String, String>

  /**
   * Instantiate a new builder.
   *
   * @param Tags: The Tags to start with.
   */
  public constructor(tags: MutableMap<String, String>) {
    this.tags = tags
  }

  public constructor() {
    this.tags = mutableMapOf<String, String>()
  }

  /**
   * Append a value to the Tag name.
   *
   * @param name: The Tag name.
   * @param value: The value associated to the Tag name.
   * @return TagsBuilder, This builder.
   */
  public fun add(name: String, value: String): TagsBuilder {
    tags.getOrPut(name) { value }
    return this
  }

  /**
   * Replace all values at the provided name with a new set of Tag values.
   *
   * @param name: The Tag name.
   * @param value: The value associated to the Tag name.
   * @return TagsBuilder, This builder.
   */
  public fun set(name: String, value: String): TagsBuilder {
    tags[name] = value
    return this
  }

  /**
   * Remove all Tags with this name.
   *
   * @param name: The Tag name to remove.
   * @return TagsBuilder, This builder.
   */
  public fun remove(name: String): TagsBuilder {
    tags.remove(name)
    return this
  }

  /**
   * Adds all tags from map to this builder.
   *
   * @param tags: A map of tags.
   * @return TagsBuilder, This builder.
   */
  public fun putAll(tags: Map<String, String>): TagsBuilder {
    this.tags.putAll(tags)
    return this
  }

  /**
   * Build the tags using the current builder.
   *
   * @return Tags, New instance of Tags.
   */
  public fun build(): Tags {
    return Tags(tags)
  }
}

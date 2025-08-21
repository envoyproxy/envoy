package io.envoyproxy.envoymobile

/** A time series counter. */
interface Counter {

  /** Increments the counter by the given count. */
  fun increment(count: Int = 1)

  /** Increments the counter by the given count and tags. */
  fun increment(tags: Tags = TagsBuilder().build(), count: Int = 1)
}

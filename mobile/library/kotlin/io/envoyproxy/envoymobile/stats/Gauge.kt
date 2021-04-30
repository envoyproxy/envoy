package io.envoyproxy.envoymobile

/** A time series gauge. */
interface Gauge {

  /** Sets the gauge by the given value. */
  fun set(value: Int)

  /** Sets the gauge by the given value along with tags. */
  fun set(tags: Tags = TagsBuilder().build(), value: Int)

  /** Adds the given amount to the gauge. */
  fun add(amount: Int)

  /** Adds the given amount to the gauge along with tags. */
  fun add(tags: Tags = TagsBuilder().build(), amount: Int)

  /** Subtracts the given amount from the gauge. */
  fun sub(amount: Int)

  /** Subtracts the given amount from the gauge along with tags. */
  fun sub(tags: Tags = TagsBuilder().build(), amount: Int)
}

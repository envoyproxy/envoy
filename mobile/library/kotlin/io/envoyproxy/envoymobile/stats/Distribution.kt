package io.envoyproxy.envoymobile

/** A time-series distribution that tracks quantile/sum/average stats. */
interface Distribution {

  /** Records a new value to add to the distribution. */
  fun recordValue(value: Int)

  /** Records a new value to add to the distribution along with the tags. */
  fun recordValue(tags: Tags = TagsBuilder().build(), value: Int)
}

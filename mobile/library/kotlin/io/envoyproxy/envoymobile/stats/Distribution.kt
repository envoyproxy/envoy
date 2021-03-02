package io.envoyproxy.envoymobile

/** A time-series distribution that tracks quantile/sum/average stats. */
interface Distribution {

  /** Record a new value to add to the distribution. */
  fun recordValue(value: Int)
}

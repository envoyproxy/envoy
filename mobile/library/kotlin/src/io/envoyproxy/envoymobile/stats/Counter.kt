package io.envoyproxy.envoymobile

/**
 * A time series counter.
 */
interface Counter {
  /**
   * Increment the counter by the given count.
   */
  fun increment(count: Int = 1)
}

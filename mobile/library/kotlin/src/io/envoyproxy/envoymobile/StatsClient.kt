package io.envoyproxy.envoymobile

/**
 * Client used to record time series metrics.
 *
 * Note: this is an experimental interface and is subject to change The implementation has not been
 * optimized, and there may be performance implications in production usage.
 */
interface StatsClient {

  /**
   * @return A counter based on the joined elements.
   */
  fun counter(vararg elements: Element): Counter

  /** @return A gauge based on the joined elements. */
  fun gauge(vararg elements: Element): Gauge
}

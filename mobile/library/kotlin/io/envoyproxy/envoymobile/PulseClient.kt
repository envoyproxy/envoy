package io.envoyproxy.envoymobile

/**
 * Client for Envoy Mobile's stats and logging library, Pulse, used to record client time series
 * metrics and logs.
 *
 * Note: this is an experimental interface and is subject to change The implementation has not been
 * optimized, and there may be performance implications in production usage.
 */
interface PulseClient {

  /**
   * @return A counter based on the joined elements.
   */
  fun counter(vararg elements: Element): Counter

  /**
   * @return A counter based on the joined elements with tags.
   */
  fun counter(vararg elements: Element, tags: Tags): Counter

  /** @return A gauge based on the joined elements. */
  fun gauge(vararg elements: Element): Gauge

  /**
   * @return A gauge based on the joined elements with tags.
   */
  fun gauge(vararg elements: Element, tags: Tags): Gauge

  /** @return A timer based on the joined elements that can track distribution of durations. */
  fun timer(vararg elements: Element): Timer

  /** @return A timer based on the joined elements with tags.
   *          It tracks distribution of durations.
   */
  fun timer(vararg elements: Element, tags: Tags): Timer

  /** @return A distribution based on the joined elements tracking the quantile stats of values. */
  fun distribution(vararg elements: Element): Distribution

  /** @return A distribution based on the joined elements with tags.
   *          It tracks the quantile stats of values.
   */
  fun distribution(vararg elements: Element, tags: Tags): Distribution

  /**
   * Logs with level, message and tags.
   *
   * @param level Level of the log
   * @param message The log message
   * @param logTags Log tags, carries miscellaneous information about the log, such as file name,
   *                line number etc.
   */
  fun log(level: LogLevel, message: String, logTags: Map<String, String>?)
}

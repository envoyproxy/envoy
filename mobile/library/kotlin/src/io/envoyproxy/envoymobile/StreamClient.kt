package io.envoyproxy.envoymobile

/**
 * Client used to create new streams.
 */
interface StreamClient {
  /**
   * Create a new stream prototype which can be used to start streams.
   *
   * @return The new stream prototype.
   */
  fun newStreamPrototype(): StreamPrototype
}

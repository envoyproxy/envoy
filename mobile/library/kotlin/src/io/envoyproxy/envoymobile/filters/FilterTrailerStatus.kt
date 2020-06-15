package io.envoyproxy.envoymobile

/*
 * Status returned by filters when transmitting or receiving trailers.
 */
// TODO: create abstract Trailers class.
sealed class FilterTrailerStatus<T : Headers> {
  /**
   * Continue filter chain iteration, passing the provided trailers through.
   */
  class Continue<T : Headers>(val trailers: T) : FilterTrailerStatus<T>()

  /**
   * Do not iterate to any of the remaining filters in the chain with trailers.
   *
   * Calling `continueRequest()`/`continueResponse()` MUST occur when continued filter iteration
   * is desired.
   */
  class StopIteration<T : Headers>(val trailers: T) : FilterTrailerStatus<T>()
}

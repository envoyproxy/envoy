package io.envoyproxy.envoymobile

/**
 * Engine represents a running instance of Envoy Mobile, and provides client interfaces that run on
 * that instance.
 */
interface Engine {

  /**
   *  @return a {@link StreamClient} for opening and managing HTTP streams.
   */
  fun streamClient(): StreamClient

  /**
   *  @return a {@link PulseClient} for recording time series metrics.
   */
  fun pulseClient(): PulseClient

  /**
   * Terminates the running engine.
   */
  fun terminate()

  /**
   * Flush the stats sinks outside of a flushing interval.
   * Note: stat flushing is done asynchronously, this function will never block.
   * This is a noop if called before the underlying EnvoyEngine has started.
   */
  fun flushStats()

  /**
   * Retrieve the value of all active stats. Note that this function may block for some time.
   * @return The list of active stats and their values, or empty string of the operation failed
   */
  fun dumpStats(): String

  /**
   * Refresh DNS, and drain connections owned by this Engine.
   */
  fun resetConnectivityState()
}

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
}

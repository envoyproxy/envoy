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
   *  @return a {@link StatsClient} for recording time series metrics.
   */
  fun statsClient(): StatsClient
}

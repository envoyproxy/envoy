package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

/* Wrapper layer for calling into Envoy's C/++ API. */
public interface EnvoyEngine {
  /**
   * Creates a new stream with the provided callbacks.
   *
   * @param callbacks The callbacks for receiving callbacks from the stream.
   * @return A stream that may be used for sending data.
   */
  EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks);

  /**
   * Run the Envoy engine with the provided yaml string and log level.
   *
   * @param configurationYAML The configuration yaml with which to start Envoy.
   * @param logLevel          The log level to use when starting Envoy.
   * @param onEngineRunning   Called when the engine finishes its async startup and begins running.
   * @return A status indicating if the action was successful.
   */
  int runWithConfig(String configurationYAML, String logLevel,
                    EnvoyOnEngineRunning onEngineRunning);

  /**
   * Run the Envoy engine with the provided EnvoyConfiguration and log level.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel           The log level to use when starting Envoy.
   * @param onEngineRunning    Called when the engine finishes its async startup and begins running.
   * @return A status indicating if the action was successful.
   */
  int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel,
                    EnvoyOnEngineRunning onEngineRunning);

  /**
   * Increments a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param count    Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  int recordCounterInc(String elements, int count);

  /**
   * Set a gauge of a given string of elements with the given value.
   *
   * @param elements Elements of the gauge stat.
   * @param value Value to set to the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeSet(String elements, int value);

  /**
   * Add the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param amount Amount to add to the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeAdd(String elements, int amount);

  /**
   * Subtract from the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param amount Amount to subtract from the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeSub(String elements, int amount);

  int registerStringAccessor(String accessor_name, EnvoyStringAccessor accessor);
}

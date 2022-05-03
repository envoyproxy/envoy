package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

import java.util.Map;

/* Wrapper layer for calling into Envoy's C/++ API. */
public interface EnvoyEngine {
  /**
   * Creates a new stream with the provided callbacks.
   *
   * @param callbacks The callbacks for receiving callbacks from the stream.
   * @param explicitFlowControl Whether explicit flow control will be enabled for this stream.
   * @return A stream that may be used for sending data.
   */
  EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks, boolean explicitFlowControl);

  /**
   * Terminates the running engine.
   */
  void terminate();

  /**
   * Run the Envoy engine with the provided yaml string and log level.
   *
   * The envoyConfiguration is used to resolve the configurationYAML.
   *
   * @param configurationYAML The configuration yaml with which to start Envoy.
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel          The log level to use when starting Envoy.
   * @return A status indicating if the action was successful.
   */
  int runWithTemplate(String configurationYAML, EnvoyConfiguration envoyConfiguration,
                      String logLevel);

  /**
   * Run the Envoy engine with the provided EnvoyConfiguration and log level.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel           The log level to use when starting Envoy.
   * @return A status indicating if the action was successful.
   */
  int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel);

  /**
   * Increments a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param tags     Tags of the counter stat.
   * @param count    Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  int recordCounterInc(String elements, Map<String, String> tags, int count);

  /**
   * Set a gauge of a given string of elements with the given value.
   *
   * @param elements Elements of the gauge stat.
   * @param tags Tags of the gauge stat.
   * @param value Value to set to the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeSet(String elements, Map<String, String> tags, int value);

  /**
   * Add the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param tags Tags of the gauge stat.
   * @param amount Amount to add to the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeAdd(String elements, Map<String, String> tags, int amount);

  /**
   * Subtract from the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param tags Tags of the gauge stat.
   * @param amount Amount to subtract from the gauge.
   * @return A status indicating if the action was successful.
   */
  int recordGaugeSub(String elements, Map<String, String> tags, int amount);

  /**
   * Add another recorded duration in ms to the timer histogram with the given string of elements.
   *
   * @param elements Elements of the histogram stat.
   * @param tags Tags of the histogram stat.
   * @param durationMs Duration value to record in the histogram timer distribution.
   * @return A status indicating if the action was successful.
   */
  int recordHistogramDuration(String elements, Map<String, String> tags, int durationMs);

  /**
   * Add another recorded value to the generic histogram with the given string of elements.
   *
   * @param elements Elements of the histogram stat.
   * @param tags Tags of the histogram stat.
   * @param value Amount to record as a new value for the histogram distribution.
   * @return A status indicating if the action was successful.
   */
  int recordHistogramValue(String elements, Map<String, String> tags, int value);

  int registerStringAccessor(String accessor_name, EnvoyStringAccessor accessor);

  /**
   * Flush the stats sinks outside of a flushing interval.
   * Note: stat flushing is done asynchronously, this function will never block.
   * This is a noop if called before the underlying EnvoyEngine has started.
   */
  void flushStats();

  String dumpStats();

  /**
   * Refresh DNS, and drain connections owned by this Engine.
   */
  void resetConnectivityState();

  /**
   * Update the network interface to the preferred network for opening new
   * streams.
   *
   * @param network The network to be preferred for new streams.
   */
  void setPreferredNetwork(EnvoyNetworkType network);
}

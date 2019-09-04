package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

public interface EnvoyEngine {
  /**
   * Creates a new stream with the provided callbacks.
   *
   * @param callbacks The callbacks for receiving callbacks from the stream.
   * @return A stream that may be used for sending data.
   */
  EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks);

  /**
   * Run the Envoy engine with the provided config and log level.
   *
   * @param config The configuration file with which to start Envoy.
   * @return A status indicating if the action was successful.
   */
  int runWithConfig(String config);

  /**
   * Run the Envoy engine with the provided config and log level.
   *
   * @param config   The configuration file with which to start Envoy.
   * @param logLevel The log level to use when starting Envoy.
   * @return int A status indicating if the action was successful.
   */
  int runWithConfig(String config, String logLevel);
}

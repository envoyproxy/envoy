package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

/* Concrete implementation of the `EnvoyEngine` interface. */
public class EnvoyEngineImpl implements EnvoyEngine {
  // TODO(goaway): enforce agreement values in /library/common/types/c_types.h.
  private static final int ENVOY_SUCCESS = 0;
  private static final int ENVOY_FAILURE = 1;

  private final long engineHandle;

  public EnvoyEngineImpl() {
    JniLibrary.load();
    this.engineHandle = JniLibrary.initEngine();
  }

  /**
   * Creates a new stream with the provided callbacks.
   *
   * @param callbacks The callbacks for the stream.
   * @return A stream that may be used for sending data.
   */
  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks) {
    long streamHandle = JniLibrary.initStream(engineHandle);
    EnvoyHTTPStream stream = new EnvoyHTTPStream(streamHandle, callbacks);
    stream.start();
    return stream;
  }

  /**
   * Run the Envoy engine with the provided yaml string and log level.
   *
   * @param configurationYAML The configuration yaml with which to start Envoy.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int runWithConfig(String configurationYAML, String logLevel) {
    try {
      return JniLibrary.runEngine(this.engineHandle, configurationYAML, logLevel);
    } catch (Throwable throwable) {
      // TODO: Need to have a way to log the exception somewhere.
      return ENVOY_FAILURE;
    }
  }

  /**
   * Run the Envoy engine with the provided envoyConfiguration and log level.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel           The log level to use when starting Envoy.
   * @return int A status indicating if the action was successful.
   */
  @Override
  public int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel) {
    return runWithConfig(envoyConfiguration.resolveTemplate(JniLibrary.templateString()), logLevel);
  }

  /**
   * Increment a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param count Amount to add to the counter.
   */
  @Override
  public void recordCounter(String elements, int count) {
    JniLibrary.recordCounter(elements, count);
  }
}

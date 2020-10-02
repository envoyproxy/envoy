package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;

/* Concrete implementation of the `EnvoyEngine` interface. */
public class EnvoyEngineImpl implements EnvoyEngine {
  // TODO(goaway): enforce agreement values in /library/common/types/c_types.h.
  private static final int ENVOY_SUCCESS = 0;
  private static final int ENVOY_FAILURE = 1;

  private final long engineHandle;
  private EnvoyOnEngineRunning onEngineRunning = () -> { return null; };

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
   * @param logLevel           The log level to use when starting Envoy.
   * @param onEngineRunning    Called when the engine finishes its async startup and begins running.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int runWithConfig(String configurationYAML, String logLevel,
                           EnvoyOnEngineRunning onEngineRunning) {
    this.onEngineRunning = onEngineRunning;
    try {
      return JniLibrary.runEngine(this.engineHandle, configurationYAML, logLevel,
                                  this.onEngineRunning);
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
   * @param onEngineRunning    Called when the engine finishes its async startup and begins running.
   * @return int A status indicating if the action was successful.
   */
  @Override
  public int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel,
                           EnvoyOnEngineRunning onEngineRunning) {
    for (EnvoyHTTPFilterFactory filterFactory : envoyConfiguration.httpFilterFactories) {
      JniLibrary.registerFilterFactory(filterFactory.getFilterName(),
                                       new JvmFilterFactoryContext(filterFactory));
    }

    return runWithConfig(envoyConfiguration.resolveTemplate(JniLibrary.templateString(),
                                                            JniLibrary.filterTemplateString()),
                         logLevel, onEngineRunning);
  }

  /**
   * Increment a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param count Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int recordCounter(String elements, int count) {
    return JniLibrary.recordCounter(engineHandle, elements, count);
  }

  /**
   * Set a gauge of a given string of elements with the given value.
   *
   * @param elements Elements of the gauge stat.
   * @param value Value to set to the gauge.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int recordGaugeSet(String elements, int value) {
    return JniLibrary.recordGaugeSet(engineHandle, elements, value);
  }

  /**
   * Add the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param amount Amount to add to the gauge.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int recordGaugeAdd(String elements, int amount) {
    return JniLibrary.recordGaugeAdd(engineHandle, elements, amount);
  }

  /**
   * Subtract from the gauge with the given string of elements and by the given amount.
   *
   * @param elements Elements of the gauge stat.
   * @param amount Amount to subtract from the gauge.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int recordGaugeSub(String elements, int amount) {
    return JniLibrary.recordGaugeSub(engineHandle, elements, amount);
  }
}

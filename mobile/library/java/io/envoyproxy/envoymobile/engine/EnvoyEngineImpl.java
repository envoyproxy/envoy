package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus;
import java.util.Map;

/* Concrete implementation of the `EnvoyEngine` interface. */
public class EnvoyEngineImpl implements EnvoyEngine {
  private static final int ENVOY_NET_GENERIC = 0;
  private static final int ENVOY_NET_WWAN = 1;
  private static final int ENVOY_NET_WLAN = 2;

  private final long engineHandle;

  /**
   * @param runningCallback Called when the engine finishes its async startup and begins running.
   * @param logger          The logging interface.
   * @param eventTracker    The event tracking interface.
   */
  public EnvoyEngineImpl(EnvoyOnEngineRunning runningCallback, EnvoyLogger logger,
                         EnvoyEventTracker eventTracker) {
    JniLibrary.load();
    this.engineHandle = JniLibrary.initEngine(runningCallback, logger, eventTracker);
  }

  /**
   * Creates a new stream with the provided callbacks.
   *
   * @param callbacks The callbacks for the stream.
   * @param explicitFlowControl Whether explicit flow control will be enabled for this stream.
   * @return A stream that may be used for sending data.
   */
  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks, boolean explicitFlowControl) {
    long streamHandle = JniLibrary.initStream(engineHandle);
    EnvoyHTTPStream stream =
        new EnvoyHTTPStream(engineHandle, streamHandle, callbacks, explicitFlowControl);
    stream.start();
    return stream;
  }

  @Override
  public void terminate() {
    JniLibrary.terminateEngine(engineHandle);
  }

  @Override
  public String dumpStats() {
    return JniLibrary.dumpStats(engineHandle);
  }

  /**
   * Performs various JNI registration prior to engine running.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   */
  @Override
  public void performRegistration(EnvoyConfiguration envoyConfiguration) {
    for (EnvoyHTTPFilterFactory filterFactory : envoyConfiguration.httpPlatformFilterFactories) {
      JniLibrary.registerFilterFactory(filterFactory.getFilterName(),
                                       new JvmFilterFactoryContext(filterFactory));
    }

    for (Map.Entry<String, EnvoyStringAccessor> entry :
         envoyConfiguration.stringAccessors.entrySet()) {
      JniLibrary.registerStringAccessor(entry.getKey(),
                                        new JvmStringAccessorContext(entry.getValue()));
    }

    for (Map.Entry<String, EnvoyKeyValueStore> entry :
         envoyConfiguration.keyValueStores.entrySet()) {
      JniLibrary.registerKeyValueStore(entry.getKey(),
                                       new JvmKeyValueStoreContext(entry.getValue()));
    }
  }

  /**
   * Run the Envoy engine with the provided yaml string and log level.
   *
   * This does not perform registration, and performRegistration may need to be called first.
   *
   * @param configurationYAML The configuration yaml with which to start Envoy.
   * @param logLevel          The log level to use when starting Envoy.
   * @return A status indicating if the action was successful.
   */
  @Override
  public EnvoyStatus runWithYaml(String configurationYAML, String logLevel) {
    return runWithResolvedYAML(configurationYAML, logLevel);
  }

  /**
   * Run the Envoy engine with the provided envoyConfiguration and log level.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel           The log level to use when starting Envoy.
   * @return EnvoyStatus A status indicating if the action was successful.
   */
  @Override
  public EnvoyStatus runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel) {
    performRegistration(envoyConfiguration);
    int status =
        JniLibrary.runEngine(this.engineHandle, "", envoyConfiguration.createBootstrap(), logLevel);
    if (status == 0) {
      return EnvoyStatus.ENVOY_SUCCESS;
    }
    return EnvoyStatus.ENVOY_FAILURE;
  }

  private EnvoyStatus runWithResolvedYAML(String configurationYAML, String logLevel) {
    try {
      int status = JniLibrary.runEngine(this.engineHandle, configurationYAML, 0, logLevel);
      if (status == 0) {
        return EnvoyStatus.ENVOY_SUCCESS;
      }
    } catch (Throwable throwable) {
      // TODO: Need to have a way to log the exception somewhere.
    }
    return EnvoyStatus.ENVOY_FAILURE;
  }

  /**
   * Increment a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param tags Tags of the counter stat.
   * @param count Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  @Override
  public int recordCounterInc(String elements, Map<String, String> tags, int count) {
    return JniLibrary.recordCounterInc(engineHandle, elements, JniBridgeUtility.toJniTags(tags),
                                       count);
  }

  @Override
  public int registerStringAccessor(String accessor_name, EnvoyStringAccessor accessor) {
    return JniLibrary.registerStringAccessor(accessor_name, new JvmStringAccessorContext(accessor));
  }

  @Override
  public void resetConnectivityState() {
    JniLibrary.resetConnectivityState(engineHandle);
  }

  @Override
  public void setPreferredNetwork(EnvoyNetworkType network) {
    switch (network) {
    case ENVOY_NETWORK_TYPE_WWAN:
      JniLibrary.setPreferredNetwork(engineHandle, ENVOY_NET_WWAN);
      return;
    case ENVOY_NETWORK_TYPE_WLAN:
      JniLibrary.setPreferredNetwork(engineHandle, ENVOY_NET_WLAN);
      return;
    case ENVOY_NETWORK_TYPE_GENERIC:
      JniLibrary.setPreferredNetwork(engineHandle, ENVOY_NET_GENERIC);
      return;
    default:
      JniLibrary.setPreferredNetwork(engineHandle, ENVOY_NET_GENERIC);
      return;
    }
  }

  public void setProxySettings(String host, int port) {
    JniLibrary.setProxySettings(engineHandle, host, port);
  }

  @Override
  public void setLogLevel(LogLevel log_level) {
    JniLibrary.setLogLevel(log_level.ordinal());
  }
}

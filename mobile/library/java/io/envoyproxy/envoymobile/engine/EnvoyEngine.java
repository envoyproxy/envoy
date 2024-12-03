package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus;

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
   * Performs any registrations necessary before running Envoy.
   *
   * The envoyConfiguration is used to determined what to register.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   */
  void performRegistration(EnvoyConfiguration envoyConfiguration);

  /**
   * Run the Envoy engine with the provided EnvoyConfiguration and log level.
   *
   * This automatically performs any necessary registrations.
   *
   * @param envoyConfiguration The EnvoyConfiguration used to start Envoy.
   * @param logLevel           The log level to use when starting Envoy.
   * @return A status indicating if the action was successful.
   */
  EnvoyStatus runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel);

  /**
   * Increments a counter with the given count.
   *
   * @param elements Elements of the counter stat.
   * @param tags     Tags of the counter stat.
   * @param count    Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  int recordCounterInc(String elements, Map<String, String> tags, int count);

  int registerStringAccessor(String accessor_name, EnvoyStringAccessor accessor);

  String dumpStats();

  /**
   * Refresh DNS, and drain connections owned by this Engine.
   */
  void resetConnectivityState();

  /**
   * A callback into the Envoy Engine when the default network is available.
   */
  void onDefaultNetworkAvailable();

  /**
   * A callback into the Envoy Engine when the default network type was changed.
   */
  void onDefaultNetworkChanged(EnvoyNetworkType network);

  /**
   * A callback into the Envoy Engine when the default network is unavailable.
   */
  void onDefaultNetworkUnavailable();

  /**
   * Update proxy settings.
   *
   * @param host The proxy host defined as a hostname or an IP address. Android
   *             allow users to specify proxy using either one of these.
   * @param port The proxy port.
   */
  void setProxySettings(String host, int port);

  /*
   * These are the available log levels for Envoy Mobile.
   */
  enum LogLevel { TRACE, DEBUG, INFO, WARN, ERR, CRITICAL, OFF }

  /**
   * Set the log level for Envoy mobile
   *
   * @param log_level the verbosity of logging Envoy should use.
   */
  void setLogLevel(LogLevel log_level);
}

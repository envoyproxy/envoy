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
   * @param minDeliverySize If nonzero, indicates the smallest number of response body bytes which
   *     should be delivered sans end stream.
   * @return A stream that may be used for sending data.
   */
  EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks, boolean explicitFlowControl,
                              long minDeliverySize);

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
   * Run the Envoy engine with the provided yaml string and log level.
   *
   * This does not perform registration, and performRegistration() may need to be called first.
   *
   * @param configurationYAML The configuration yaml with which to start Envoy.
   * @param logLevel          The log level to use when starting Envoy.
   * @return A status indicating if the action was successful.
   */
  EnvoyStatus runWithYaml(String configurationYAML, String logLevel);

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

  /**
   * Update proxy settings.
   *
   * @param host The proxy host defined as a hostname or an IP address. Android
   *             allow users to specify proxy using either one of these.
   * @param port The proxy port.
   */
  void setProxySettings(String host, int port);
}

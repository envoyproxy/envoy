package io.envoyproxy.envoymobile.mocks

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import io.envoyproxy.envoymobile.engine.types.EnvoyConnectionType
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor

/**
 * Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
 */
class MockEnvoyEngine : EnvoyEngine {
  override fun runWithConfig(
    envoyConfiguration: EnvoyConfiguration?,
    logLevel: String?
  ): EnvoyStatus = EnvoyStatus.ENVOY_SUCCESS

  override fun performRegistration(envoyConfiguration: EnvoyConfiguration) = Unit

  override fun startStream(
    callbacks: EnvoyHTTPCallbacks?,
    explicitFlowControl: Boolean
  ): EnvoyHTTPStream {
    return MockEnvoyHTTPStream(callbacks!!, explicitFlowControl)
  }

  override fun terminate() = Unit

  override fun recordCounterInc(
    elements: String,
    tags: MutableMap<String, String>,
    count: Int
  ): Int = 0

  override fun registerStringAccessor(accessorName: String, accessor: EnvoyStringAccessor): Int = 0

  override fun dumpStats(): String = ""

  override fun resetConnectivityState() = Unit

  override fun onDefaultNetworkAvailable() = Unit

  override fun onDefaultNetworkChanged(network: Int) = Unit

  override fun onDefaultNetworkChangeEvent(network: Int) = Unit

  override fun onDefaultNetworkChangedV2(network_type: EnvoyConnectionType, net_id: Long) = Unit

  override fun onNetworkDisconnect(net_id: Long) = Unit

  override fun onNetworkConnect(network_type: EnvoyConnectionType, net_id: Long) = Unit

  override fun purgeActiveNetworkList(activeNetIds: LongArray) = Unit

  override fun onDefaultNetworkUnavailable() = Unit

  override fun setProxySettings(host: String, port: Int) = Unit

  override fun setLogLevel(level: EnvoyEngine.LogLevel) = Unit
}

package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus

/** An implementation of {@link Engine}. */
class EngineImpl
constructor(
  internal val envoyEngine: EnvoyEngine,
  internal val envoyConfiguration: EnvoyConfiguration,
  internal val configurationYAML: String?,
  internal val logLevel: LogLevel
) : Engine {

  private val streamClient: StreamClient
  private val pulseClient: PulseClient

  constructor(
    envoyEngine: EnvoyEngine,
    envoyConfiguration: EnvoyConfiguration,
    logLevel: LogLevel = LogLevel.INFO
  ) : this(envoyEngine, envoyConfiguration, null, logLevel)

  init {
    streamClient = StreamClientImpl(envoyEngine)
    pulseClient = PulseClientImpl(envoyEngine)
    val envoyStatus =
      if (configurationYAML != null) {
        envoyEngine.performRegistration(envoyConfiguration)
        envoyEngine.runWithYaml(configurationYAML, logLevel.level)
      } else {
        envoyEngine.runWithConfig(envoyConfiguration, logLevel.level)
      }
    if (envoyStatus == EnvoyStatus.ENVOY_FAILURE) {
      throw IllegalStateException("Unable to start Envoy.")
    }
  }

  override fun streamClient(): StreamClient {
    return streamClient
  }

  override fun pulseClient(): PulseClient {
    return pulseClient
  }

  override fun terminate() {
    envoyEngine.terminate()
  }

  override fun dumpStats(): String {
    return envoyEngine.dumpStats()
  }

  override fun resetConnectivityState() {
    envoyEngine.resetConnectivityState()
  }
}

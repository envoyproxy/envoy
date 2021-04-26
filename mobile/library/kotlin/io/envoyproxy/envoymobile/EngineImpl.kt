package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * An implementation of {@link Engine}.
 */
class EngineImpl internal constructor(
  internal val envoyEngine: EnvoyEngine,
  internal val envoyConfiguration: EnvoyConfiguration?,
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

  constructor(
    envoyEngine: EnvoyEngine,
    configurationYAML: String,
    logLevel: LogLevel = LogLevel.INFO
  ) : this(envoyEngine, null, configurationYAML, logLevel)

  init {
    streamClient = StreamClientImpl(envoyEngine)
    pulseClient = PulseClientImpl(envoyEngine)
    if (envoyConfiguration == null) {
      envoyEngine.runWithConfig(configurationYAML, logLevel.level)
    } else {
      envoyEngine.runWithConfig(envoyConfiguration, logLevel.level)
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
}

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
  internal val logLevel: LogLevel,
  internal val onEngineRunning: (() -> Unit)?
) : Engine {

  private val streamClient: StreamClient
  private val statsClient: StatsClient

  constructor(
    envoyEngine: EnvoyEngine,
    envoyConfiguration: EnvoyConfiguration,
    logLevel: LogLevel = LogLevel.INFO,
    onEngineRunning: (() -> Unit)?
  ) : this(envoyEngine, envoyConfiguration, null, logLevel, onEngineRunning)

  constructor(
    envoyEngine: EnvoyEngine,
    configurationYAML: String,
    logLevel: LogLevel = LogLevel.INFO,
    onEngineRunning: (() -> Unit)?
  ) : this(envoyEngine, null, configurationYAML, logLevel, onEngineRunning)

  init {
    streamClient = StreamClientImpl(envoyEngine)
    statsClient = StatsClientImpl(envoyEngine)
    if (envoyConfiguration == null) {
      envoyEngine.runWithConfig(configurationYAML, logLevel.level, onEngineRunning)
    } else {
      envoyEngine.runWithConfig(envoyConfiguration, logLevel.level, onEngineRunning)
    }
  }

  override fun streamClient(): StreamClient {
    return streamClient
  }

  override fun statsClient(): StatsClient {
    return statsClient
  }
}

package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * Envoy's implementation of `StreamClient`, buildable using `StreamClientBuilder`.
 */
internal class EnvoyClient private constructor(
  internal val engine: EnvoyEngine,
  internal val envoyConfiguration: EnvoyConfiguration?,
  internal val configurationYAML: String?,
  internal val logLevel: LogLevel
) : StreamClient {
  constructor(
    engine: EnvoyEngine,
    envoyConfiguration: EnvoyConfiguration,
    logLevel: LogLevel = LogLevel.INFO
  ) : this(engine, envoyConfiguration, null, logLevel)
  constructor(
    engine: EnvoyEngine,
    configurationYAML: String,
    logLevel: LogLevel = LogLevel.INFO
  ) : this(engine, null, configurationYAML, logLevel)

  init {
    if (envoyConfiguration == null) {
      engine.runWithConfig(configurationYAML, logLevel.level)
    } else {
      engine.runWithConfig(envoyConfiguration, logLevel.level)
    }
  }

  override fun newStreamPrototype() = StreamPrototype(engine)
}

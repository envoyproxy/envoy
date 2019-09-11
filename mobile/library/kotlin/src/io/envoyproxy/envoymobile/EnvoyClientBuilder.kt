package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyEngineImpl


open class EnvoyClientBuilder internal constructor(
) {
  private var logLevel = LogLevel.INFO
  private var configYAML: String? = null
  private var engineType: () -> EnvoyEngine = { EnvoyEngineImpl() }

  private var connectTimeoutSeconds = 30
  private var dnsRefreshSeconds = 60
  private var statsFlushSeconds = 60

  /**
   * Add a log level to use with Envoy.
   * @param logLevel the log level to use with Envoy.
   */
  fun addLogLevel(logLevel: LogLevel): EnvoyClientBuilder {
    this.logLevel = logLevel
    return this
  }

  /**
   * Add contents of a yaml file to use as a configuration.
   * Setting this will supersede any other configuration settings in the builder.
   *
   * @param configYAML the contents of a yaml file to use as a configuration.
   */
  fun addConfigYAML(configYAML: String?): EnvoyClientBuilder {
    this.configYAML = configYAML
    return this
  }

  /**
   * Add a timeout for new network connections to hosts in the cluster.
   *
   * @param connectTimeoutSeconds timeout for new network connections to hosts in the cluster.
   */
  fun addConnectTimeoutSeconds(connectTimeoutSeconds: Int): EnvoyClientBuilder {
    this.connectTimeoutSeconds = connectTimeoutSeconds
    return this
  }

  /**
   * Add a rate at which to refresh DNS.
   *
   * @param dnsRefreshSeconds rate in seconds to refresh DNS.
   */
  fun addDNSRefreshSeconds(dnsRefreshSeconds: Int): EnvoyClientBuilder {
    this.dnsRefreshSeconds = dnsRefreshSeconds
    return this
  }

  /**
   * Add an interval at which to flush Envoy stats.
   *
   * @param statsFlushSeconds interval at which to flush Envoy stats.
   */
  fun addStatsFlushSeconds(statsFlushSeconds: Int): EnvoyClientBuilder {
    this.statsFlushSeconds = statsFlushSeconds
    return this
  }

  /**
   * Builds a new instance of Envoy using the provided configurations.
   *
   * @return A new instance of Envoy.
   */
  fun build(): Envoy {
    val configurationYAML = configYAML
    if (configurationYAML == null) {
      return Envoy(engineType(), EnvoyConfiguration(connectTimeoutSeconds, dnsRefreshSeconds, statsFlushSeconds), logLevel)
    } else {
      return Envoy(engineType(), configurationYAML, logLevel)
    }
  }

  /**
   * Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
   *
   * A new instance of this engine will be created when `build()` is called.
   */
  internal fun addEngineType(engineType: () -> EnvoyEngine): EnvoyClientBuilder {
    this.engineType = engineType
    return this
  }
}

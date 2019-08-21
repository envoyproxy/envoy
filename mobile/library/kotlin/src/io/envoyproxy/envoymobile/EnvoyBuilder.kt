package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyConfigurationImpl
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyEngineImpl

class ConfigurationException : Exception("Unresolved Template Key")

open class EnvoyBuilder internal constructor(
    private val envoyConfiguration: EnvoyConfiguration
) {
  private var logLevel = LogLevel.INFO
  private var configYAML: String
  private var engineType: () -> EnvoyEngine = { EnvoyEngineImpl() }

  private var connectTimeoutSeconds = 30
  private var dnsRefreshSeconds = 60
  private var statsFlushSeconds = 60

  constructor() : this(EnvoyConfigurationImpl())

  init {
    configYAML = envoyConfiguration.templateString()
  }

  /**
   * Add a log level to use with Envoy.
   * @param logLevel the log level to use with Envoy.
   */
  fun addLogLevel(logLevel: LogLevel): EnvoyBuilder {
    this.logLevel = logLevel
    return this
  }

  /**
   * Add a YAML file to use as a configuration.
   * Setting this will supersede any other configuration settings in the builder.
   *
   * @param configYAML the YAML file to use as a configuration.
   */
  fun addConfigYAML(configYAML: String?): EnvoyBuilder {
    this.configYAML = configYAML ?: envoyConfiguration.templateString()
    return this
  }

  /**
   * Add a timeout for new network connections to hosts in the cluster.
   *
   * @param connectTimeoutSeconds timeout for new network connections to hosts in the cluster.
   */
  fun addConnectTimeoutSeconds(connectTimeoutSeconds: Int): EnvoyBuilder {
    this.connectTimeoutSeconds = connectTimeoutSeconds
    return this
  }

  /**
   * Add a rate at which to refresh DNS.
   *
   * @param dnsRefreshSeconds rate in seconds to refresh DNS.
   */
  fun addDNSRefreshSeconds(dnsRefreshSeconds: Int): EnvoyBuilder {
    this.dnsRefreshSeconds = dnsRefreshSeconds
    return this
  }

  /**
   * Add an interval at which to flush Envoy stats.
   *
   * @param statsFlushSeconds interval at which to flush Envoy stats.
   */
  fun addStatsFlushSeconds(statsFlushSeconds: Int): EnvoyBuilder {
    this.statsFlushSeconds = statsFlushSeconds
    return this
  }

  /**
   * Builds a new instance of Envoy using the provided configurations.
   *
   * @return A new instance of Envoy.
   */
  fun build(): Envoy {
    return Envoy(engineType(), resolvedYAML(), logLevel)
  }

  /**
   * Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
   *
   * A new instance of this engine will be created when `build()` is called.
   */
  internal fun addEngineType(engineType: () -> EnvoyEngine): EnvoyBuilder {
    this.engineType = engineType
    return this
  }


  /** Processes the YAML template provided, replacing keys with values from the configuration.
   *
   * @parameter template: The template YAML file to use.
   * @returns: A resolved YAML file with all template keys replaced.
   * @throws ConfigurationException when the yaml configuration replacement is incomplete
   */
  private fun resolvedYAML(): String {
    val resolvedTemplate = configYAML
        .replace("{{ connect_timeout }}", "${connectTimeoutSeconds}s")
        .replace("{{ dns_refresh_rate }}", "${dnsRefreshSeconds}s")
        .replace("{{ stats_flush_interval }}", "${statsFlushSeconds}s")

    if (resolvedTemplate.contains("{{")) {
      throw ConfigurationException()
    }

    return resolvedTemplate
  }
}

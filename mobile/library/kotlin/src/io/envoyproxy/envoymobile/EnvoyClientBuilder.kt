package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyEngineImpl

sealed class BaseConfiguration

class Standard : BaseConfiguration()
class Custom(val yaml: String) : BaseConfiguration()

open class EnvoyClientBuilder(
    private val configuration: BaseConfiguration = Standard()
) {
  private var logLevel = LogLevel.INFO
  private var engineType: () -> EnvoyEngine = { EnvoyEngineImpl() }

  private var statsDomain = "0.0.0.0"
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
   * Add a domain to flush stats to.
   * @param statsDomain the domain to flush stats to.
   */
  fun addStatsDomain(statsDomain: String): EnvoyClientBuilder {
    this.statsDomain = statsDomain
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
    return when (configuration) {
      is Custom -> {
        return Envoy(engineType(), configuration.yaml, logLevel)
      }
      is Standard -> {
        Envoy(engineType(), EnvoyConfiguration(statsDomain, connectTimeoutSeconds, dnsRefreshSeconds, statsFlushSeconds), logLevel)
      }
    }
  }

  /**
   * Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
   *
   * A new instance of this engine will be created when `build()` is called.
   */
  fun addEngineType(engineType: () -> EnvoyEngine): EnvoyClientBuilder {
    this.engineType = engineType
    return this
  }
}

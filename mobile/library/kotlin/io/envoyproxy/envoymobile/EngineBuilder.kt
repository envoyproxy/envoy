package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyEngineImpl
import io.envoyproxy.envoymobile.engine.EnvoyNativeFilterConfig
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor
import java.util.UUID

sealed class BaseConfiguration

class Standard : BaseConfiguration()
class Custom(val yaml: String) : BaseConfiguration()

/**
 * Builder used for creating and running a new `Engine` instance.
 */
open class EngineBuilder(
  private val configuration: BaseConfiguration = Standard()
) {
  protected var onEngineRunning: (() -> Unit) = {}
  protected var logger: ((String) -> Unit)? = null
  private var engineType: () -> EnvoyEngine = { EnvoyEngineImpl(onEngineRunning, logger) }
  private var logLevel = LogLevel.INFO
  private var grpcStatsDomain: String? = null
  private var statsDPort: Int? = null
  private var connectTimeoutSeconds = 30
  private var dnsRefreshSeconds = 60
  private var dnsFailureRefreshSecondsBase = 2
  private var dnsFailureRefreshSecondsMax = 10
  private var dnsQueryTimeoutSeconds = 25
  private var dnsPreresolveHostnames = "[]"
  private var statsFlushSeconds = 60
  private var streamIdleTimeoutSeconds = 15
  private var appVersion = "unspecified"
  private var appId = "unspecified"
  private var virtualClusters = "[]"
  private var platformFilterChain = mutableListOf<EnvoyHTTPFilterFactory>()
  private var nativeFilterChain = mutableListOf<EnvoyNativeFilterConfig>()
  private var stringAccessors = mutableMapOf<String, EnvoyStringAccessor>()

  /**
   * Add a log level to use with Envoy.
   *
   * @param logLevel the log level to use with Envoy.
   *
   * @return this builder.
   */
  fun addLogLevel(logLevel: LogLevel): EngineBuilder {
    this.logLevel = logLevel
    return this
  }

  /**
   * Add a domain to flush stats to.
   * Passing nil disables stats emission via the gRPC stat sink.
   *
   * Only one of the statsd and gRPC stat sink can be enabled.
   *
   * @param grpcStatsDomain The domain to use for stats.
   *
   * @return this builder.
   */
  fun addGrpcStatsDomain(grpcStatsDomain: String?): EngineBuilder {
    this.grpcStatsDomain = grpcStatsDomain
    return this
  }

  /**
   * Add a loopback port to emit statsD stats to.
   * Passing nil disables stats emission via the statsD stat sink.
   *
   * Only one of the statsD and gRPC stat sink can be enabled.
   *
   * @param port The port to send statsD UDP packets to via loopback
   *
   * @return this builder.
   */
  fun addStatsDPort(port: Int): EngineBuilder {
    this.statsDPort = port
    return this
  }

  /**
   * Add a timeout for new network connections to hosts in the cluster.
   *
   * @param connectTimeoutSeconds timeout for new network connections to hosts in the cluster.
   *
   * @return this builder.
   */
  fun addConnectTimeoutSeconds(connectTimeoutSeconds: Int): EngineBuilder {
    this.connectTimeoutSeconds = connectTimeoutSeconds
    return this
  }

  /**
   * Add a rate at which to refresh DNS.
   *
   * @param dnsRefreshSeconds rate in seconds to refresh DNS.
   *
   * @return this builder.
   */
  fun addDNSRefreshSeconds(dnsRefreshSeconds: Int): EngineBuilder {
    this.dnsRefreshSeconds = dnsRefreshSeconds
    return this
  }

  /**
   * Add a rate at which to refresh DNS in case of DNS failure.
   *
   * @param base rate in seconds.
   * @param max rate in seconds.
   *
   * @return this builder.
   */
  fun addDNSFailureRefreshSeconds(base: Int, max: Int): EngineBuilder {
    this.dnsFailureRefreshSecondsBase = base
    this.dnsFailureRefreshSecondsMax = max
    return this
  }

  /**
   * Add a rate at which to timeout DNS queries.
   *
   * @param dnsQueryTimeoutSeconds rate in seconds to timeout DNS queries.
   *
   * @return this builder.
   */
  fun addDNSQueryTimeoutSeconds(dnsQueryTimeoutSeconds: Int): EngineBuilder {
    this.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds
    return this
  }

  /**
   * Add a list of hostnames to preresolve on Engine startup.
   *
   * @param dnsPreresolveHostnames hostnames to preresolve.
   *
   * @return this builder.
   */
  fun addDNSPreresolveHostnames(dnsPreresolveHostnames: String): EngineBuilder {
    this.dnsPreresolveHostnames = dnsPreresolveHostnames
    return this
  }

  /**
   * Add an interval at which to flush Envoy stats.
   *
   * @param statsFlushSeconds interval at which to flush Envoy stats.
   *
   * @return this builder.
   */
  fun addStatsFlushSeconds(statsFlushSeconds: Int): EngineBuilder {
    this.statsFlushSeconds = statsFlushSeconds
    return this
  }

  /**
   * Add a custom idle timeout for HTTP streams. Defaults to 15 seconds.
   *
   * @param streamIdleTimeoutSeconds idle timeout for HTTP streams.
   *
   * @return this builder.
   */
  fun addStreamIdleTimeoutSeconds(streamIdleTimeoutSeconds: Int): EngineBuilder {
    this.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds
    return this
  }

  /**
   * Add an HTTP filter factory used to create platform filters for streams sent by this client.
   *
   * @param name Custom name to use for this filter factory. Useful for having
   *             more meaningful trace logs, but not required. Should be unique
   *             per factory registered.
   * @param factory closure returning an instantiated filter.
   *
   * @return this builder.
   */
  fun addPlatformFilter(name: String, factory: () -> Filter):
    EngineBuilder {
      this.platformFilterChain.add(FilterFactory(name, factory))
      return this
    }

  /**
   * Add an HTTP filter factory used to create platform filters for streams sent by this client.
   *
   * @param factory closure returning an instantiated filter.
   *
   * @return this builder.
   */
  fun addPlatformFilter(factory: () -> Filter):
    EngineBuilder {
      this.platformFilterChain.add(FilterFactory(UUID.randomUUID().toString(), factory))
      return this
    }

  /**
   * Add an HTTP filter config used to create native filters for streams sent by this client.
   *
   * @param name Custom name to use for this filter factory. Useful for having
   *             more meaningful trace logs, but not required. Should be unique
   *             per filter.
   * @param typedConfig config string for the filter.
   *
   * @return this builder.
   */
  fun addNativeFilter(name: String = UUID.randomUUID().toString(), typedConfig: String):
    EngineBuilder {
      this.nativeFilterChain.add(EnvoyNativeFilterConfig(name, typedConfig))
      return this
    }

  /**
   * Set a closure to be called when the engine finishes its async startup and begins running.
   *
   * @param closure the closure to be called.
   *
   * @return this builder.
   */
  fun setOnEngineRunning(closure: () -> Unit): EngineBuilder {
    this.onEngineRunning = closure
    return this
  }

  /**
   * Set a closure to be called when the engine's logger logs.
   * @param closure: The closure to be called.
   *
   * @return This builder.
   */
  fun setLogger(closure: (String) -> Unit): EngineBuilder {
    this.logger = closure
    return this
  }

  /**
   * Add a string accessor to this Envoy Client.
   *
   * @param name the name of the accessor.
   * @param accessor the string accessor.
   *
   * @return this builder.
   */
  fun addStringAccessor(name: String, accessor: () -> String): EngineBuilder {
    this.stringAccessors.put(name, EnvoyStringAccessorAdapter(StringAccessor(accessor)))
    return this
  }

  /**
   * Add the App Version of the App using this Envoy Client.
   *
   * @param appVersion the version.
   *
   * @return this builder.
   */
  fun addAppVersion(appVersion: String): EngineBuilder {
    this.appVersion = appVersion
    return this
  }

  /**
   * Add the App ID of the App using this Envoy Client.
   *
   * @param appId the ID.
   *
   * @return this builder.
   */
  fun addAppId(appId: String): EngineBuilder {
    this.appId = appId
    return this
  }

  /**
   * Add virtual cluster configuration.
   *
   * @param virtualClusters the JSON configuration string for virtual clusters.
   *
   * @return this builder.
   */
  fun addVirtualClusters(virtualClusters: String): EngineBuilder {
    this.virtualClusters = virtualClusters
    return this
  }

  /**
   * Builds and runs a new Engine instance with the provided configuration.
   *
   * @return A new instance of Envoy.
   */
  fun build(): Engine {
    return when (configuration) {
      is Custom -> {
        EngineImpl(
          engineType(),
          EnvoyConfiguration(
            grpcStatsDomain, statsDPort, connectTimeoutSeconds,
            dnsRefreshSeconds, dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax,
            dnsQueryTimeoutSeconds,
            dnsPreresolveHostnames, statsFlushSeconds, streamIdleTimeoutSeconds, appVersion, appId,
            virtualClusters, nativeFilterChain, platformFilterChain, stringAccessors
          ),
          configuration.yaml,
          logLevel
        )
      }
      is Standard -> {
        EngineImpl(
          engineType(),
          EnvoyConfiguration(
            grpcStatsDomain, statsDPort, connectTimeoutSeconds,
            dnsRefreshSeconds, dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax,
            dnsQueryTimeoutSeconds,
            dnsPreresolveHostnames, statsFlushSeconds, streamIdleTimeoutSeconds, appVersion, appId,
            virtualClusters, nativeFilterChain, platformFilterChain, stringAccessors
          ),
          logLevel
        )
      }
    }
  }

  /**
   * Add a specific implementation of `EnvoyEngine` to use for starting Envoy.
   *
   * A new instance of this engine will be created when `build()` is called.
   */
  fun addEngineType(engineType: () -> EnvoyEngine): EngineBuilder {
    this.engineType = engineType
    return this
  }
}

package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
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
  protected var eventTracker: ((Map<String, String>) -> Unit)? = null
  private var engineType: () -> EnvoyEngine = {
    EnvoyEngineImpl(onEngineRunning, logger, eventTracker)
  }
  private var logLevel = LogLevel.INFO
  private var adminInterfaceEnabled = false
  private var grpcStatsDomain: String? = null
  private var statsDPort: Int? = null
  private var connectTimeoutSeconds = 30
  private var dnsRefreshSeconds = 60
  private var dnsFailureRefreshSecondsBase = 2
  private var dnsFailureRefreshSecondsMax = 10
  private var dnsFallbackNameservers = listOf<String>()
  private var dnsFilterUnroutableFamilies = true
  private var dnsQueryTimeoutSeconds = 25
  private var dnsMinRefreshSeconds = 60
  private var dnsPreresolveHostnames = "[]"
  private var enableDrainPostDnsRefresh = false
  private var enableHttp3 = false
  private var enableHappyEyeballs = false
  private var enableInterfaceBinding = false
  private var h2ConnectionKeepaliveIdleIntervalMilliseconds = 100000000
  private var h2ConnectionKeepaliveTimeoutSeconds = 10
  private var h2ExtendKeepaliveTimeout = false
  private var h2RawDomains = listOf<String>()
  private var maxConnectionsPerHost = 7
  private var statsFlushSeconds = 60
  private var streamIdleTimeoutSeconds = 15
  private var perTryIdleTimeoutSeconds = 15
  private var appVersion = "unspecified"
  private var appId = "unspecified"
  private var trustChainVerification = TrustChainVerification.VERIFY_TRUST_CHAIN
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
   * Add a default rate at which to refresh DNS.
   *
   * @param dnsRefreshSeconds default rate in seconds at which to refresh DNS.
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
   * Add the minimum rate at which to refresh DNS. Once DNS has been resolved for a host, DNS TTL
   * will be respected, subject to this minimum. Defaults to 60 seconds.
   *
   * @param dnsMinRefreshSeconds minimum rate in seconds at which to refresh DNS.
   *
   * @return this builder.
   */
  fun addDNSMinRefreshSeconds(dnsMinRefreshSeconds: Int): EngineBuilder {
    this.dnsMinRefreshSeconds = dnsMinRefreshSeconds
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
   * Add a list of IP addresses to use as fallback DNS name servers.
   *
   * @param dnsFallbackNameservers addresses to use.
   *
   * @return this builder.
   */
  fun addDNSFallbackNameservers(dnsFallbackNameservers: List<String>): EngineBuilder {
    this.dnsFallbackNameservers = dnsFallbackNameservers
    return this
  }

  /**
   * Specify whether to filter unroutable IP families during DNS resolution or not.
   * Defaults to true.
   *
   * @param dnsFilterUnroutableFamilies whether to filter or not.
   *
   * @return this builder.
   */
  fun enableDNSFilterUnroutableFamilies(dnsFilterUnroutableFamilies: Boolean): EngineBuilder {
    this.dnsFilterUnroutableFamilies = dnsFilterUnroutableFamilies
    return this
  }

  /**
   * Specify whether to drain connections after the resolution of a soft DNS refresh. A refresh may
   * be triggered directly via the Engine API, or as a result of a network status update provided by
   * the OS. Draining connections does not interrupt existing connections or requests, but will
   * establish new connections for any further requests.
   *
   * @param enableDrainPostDnsRefresh whether to drain connections after soft DNS refresh.
   *
   * @return This builder.
   */
  fun enableDrainPostDnsRefresh(enableDrainPostDnsRefresh: Boolean): EngineBuilder {
    this.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh
    return this
  }

  /**
   * Specify whether to enable experimental HTTP/3 (QUIC) support. Note the actual protocol will
   * be negotiated with the upstream endpoint and so upstream support is still required for HTTP/3
   * to be utilized.
   *
   * @param enableHttp3 whether to enable HTTP/3.
   *
   * @return This builder.
   */
  fun enableHttp3(enableHttp3: Boolean): EngineBuilder {
    this.enableHttp3 = enableHttp3
    return this
  }

  /**
   * Specify whether to use Happy Eyeballs when multiple IP stacks may be supported.
   *
   * @param enableHappyEyeballs whether to enable RFC 6555 handling for IPv4/IPv6.
   *
   * @return This builder.
   */
  fun enableHappyEyeballs(enableHappyEyeballs: Boolean): EngineBuilder {
    this.enableHappyEyeballs = enableHappyEyeballs
    return this
  }

  /**
   * Specify whether sockets may attempt to bind to a specific interface, based on network
   * conditions.
   *
   * @param enableInterfaceBinding whether to allow interface binding.
   *
   * @return This builder.
   */
  fun enableInterfaceBinding(enableInterfaceBinding: Boolean): EngineBuilder {
    this.enableInterfaceBinding = enableInterfaceBinding
    return this
  }

  /**
   * Add a rate at which to ping h2 connections on new stream creation if the connection has
   * sat idle.
   *
   * @param h2ConnectionKeepaliveIdleIntervalMilliseconds rate in milliseconds.
   *
   * @return this builder.
   */
  fun addH2ConnectionKeepaliveIdleIntervalMilliseconds(idleIntervalMs: Int): EngineBuilder {
    this.h2ConnectionKeepaliveIdleIntervalMilliseconds = idleIntervalMs
    return this
  }

  /**
   * Add a rate at which to timeout h2 pings.
   *
   * @param h2ConnectionKeepaliveTimeoutSeconds rate in seconds to timeout h2 pings.
   *
   * @return this builder.
   */
  fun addH2ConnectionKeepaliveTimeoutSeconds(timeoutSeconds: Int): EngineBuilder {
    this.h2ConnectionKeepaliveTimeoutSeconds = timeoutSeconds
    return this
  }

  /**
   * Extend the keepalive timeout when *any* frame is received on the owning HTTP/2 connection.
   *
   * @param h2ExtendKeepaliveTimeout whether to extend the keepalive timeout.
   *
   * @return This builder.
   */
  fun h2ExtendKeepaliveTimeout(h2ExtendKeepaliveTimeout: Boolean): EngineBuilder {
    this.h2ExtendKeepaliveTimeout = h2ExtendKeepaliveTimeout
    return this
  }

  /**
   * Add a list of domains to which h2 connections will be established without protocol negotiation.
   *
   * @param h2RawDomains list of domains to which connections should be raw h2.
   *
   * @return this builder.
   */
  fun addH2RawDomains(h2RawDomains: List<String>): EngineBuilder {
    this.h2RawDomains = h2RawDomains
    return this
  }

  /**
   * Set the maximum number of connections to open to a single host. Default is 7.
   *
   * @param maxConnectionsPerHost the maximum number of connections per host.
   *
   * @return this builder.
   */
  fun setMaxConnectionsPerHost(maxConnectionsPerHost: Int): EngineBuilder {
    this.maxConnectionsPerHost = maxConnectionsPerHost
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
   * Add a custom per try idle timeout for HTTP streams. Defaults to 15 seconds.
   *
   * @param perTryIdleTimeoutSeconds per try idle timeout for HTTP streams.
   *
   * @return this builder.
   */
  fun addPerTryIdleTimeoutSeconds(perTryIdleTimeoutSeconds: Int): EngineBuilder {
    this.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds
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
   * Set event tracker for the engine to call when it emits an event.
   */
  fun setEventTracker(eventTracker: (Map<String, String>) -> Unit): EngineBuilder {
    this.eventTracker = eventTracker
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
   * Set how the TrustChainVerification must be handled.
   *
   * @param trustChainVerification whether to mute TLS Cert verification - intended for testing
   *
   * @return this builder.
   */
  fun setTrustChainVerification(trustChainVerification: TrustChainVerification): EngineBuilder {
    this.trustChainVerification = trustChainVerification
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
   * Enable admin interface on 127.0.0.1:9901 address. Admin interface is intended to be
   * used for development/debugging purposes only. Enabling it in production may open
   * your app to security vulnerabilities.
   *
   * @return this builder.
   */
  fun enableAdminInterface(): EngineBuilder {
    this.adminInterfaceEnabled = true
    return this
  }

  /**
   * Builds and runs a new Engine instance with the provided configuration.
   *
   * @return A new instance of Envoy.
   */
  @Suppress("LongMethod")
  fun build(): Engine {
    val engineConfiguration = EnvoyConfiguration(
      adminInterfaceEnabled,
      grpcStatsDomain,
      statsDPort,
      connectTimeoutSeconds,
      dnsRefreshSeconds,
      dnsFailureRefreshSecondsBase,
      dnsFailureRefreshSecondsMax,
      dnsQueryTimeoutSeconds,
      dnsMinRefreshSeconds,
      dnsPreresolveHostnames,
      dnsFallbackNameservers,
      dnsFilterUnroutableFamilies,
      enableDrainPostDnsRefresh,
      enableHttp3,
      enableHappyEyeballs,
      enableInterfaceBinding,
      h2ConnectionKeepaliveIdleIntervalMilliseconds,
      h2ConnectionKeepaliveTimeoutSeconds,
      h2ExtendKeepaliveTimeout,
      h2RawDomains,
      maxConnectionsPerHost,
      statsFlushSeconds,
      streamIdleTimeoutSeconds,
      perTryIdleTimeoutSeconds,
      appVersion,
      appId,
      trustChainVerification,
      virtualClusters,
      nativeFilterChain,
      platformFilterChain,
      stringAccessors
    )

    return when (configuration) {
      is Custom -> {
        EngineImpl(
          engineType(),
          engineConfiguration,
          configuration.yaml,
          logLevel
        )
      }
      is Standard -> {
        EngineImpl(
          engineType(),
          engineConfiguration,
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

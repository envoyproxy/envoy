package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyEngineImpl
import io.envoyproxy.envoymobile.engine.EnvoyNativeFilterConfig
import io.envoyproxy.envoymobile.engine.VirtualClusterConfig
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor
import java.util.UUID

/**
 * Envoy engine configuration.
 */
sealed class BaseConfiguration

/**
 * The standard configuration.
 */
class Standard : BaseConfiguration()

/**
 * The configuration based off a custom yaml.
 *
 * @param yaml the custom config.
 */
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
  protected var enableProxying = false
  private var runtimeGuards = mutableMapOf<String, Boolean>()
  private var enableSkipDNSLookupForProxiedRequests = false
  private var engineType: () -> EnvoyEngine = {
    EnvoyEngineImpl(onEngineRunning, logger, eventTracker)
  }
  private var logLevel = LogLevel.INFO
  internal var adminInterfaceEnabled = false
  private var grpcStatsDomain: String? = null
  private var connectTimeoutSeconds = 30
  private var dnsRefreshSeconds = 60
  private var dnsFailureRefreshSecondsBase = 2
  private var dnsFailureRefreshSecondsMax = 10
  private var dnsQueryTimeoutSeconds = 25
  private var dnsMinRefreshSeconds = 60
  private var dnsPreresolveHostnames = listOf<String>()
  private var enableDNSCache = false
  private var dnsCacheSaveIntervalSeconds = 1
  private var enableDrainPostDnsRefresh = false
  internal var enableHttp3 = true
  private var enableHappyEyeballs = true
  private var enableGzipDecompression = true
  private var enableBrotliDecompression = false
  private var enableSocketTagging = false
  private var enableInterfaceBinding = false
  private var h2ConnectionKeepaliveIdleIntervalMilliseconds = 1
  private var h2ConnectionKeepaliveTimeoutSeconds = 10
  private var maxConnectionsPerHost = 7
  private var statsFlushSeconds = 60
  private var streamIdleTimeoutSeconds = 15
  private var perTryIdleTimeoutSeconds = 15
  private var appVersion = "unspecified"
  private var appId = "unspecified"
  private var trustChainVerification = TrustChainVerification.VERIFY_TRUST_CHAIN
  private var virtualClustersLegacy = mutableListOf<String>()
  private var virtualClusters = mutableListOf<VirtualClusterConfig>()
  private var platformFilterChain = mutableListOf<EnvoyHTTPFilterFactory>()
  private var nativeFilterChain = mutableListOf<EnvoyNativeFilterConfig>()
  private var stringAccessors = mutableMapOf<String, EnvoyStringAccessor>()
  private var keyValueStores = mutableMapOf<String, EnvoyKeyValueStore>()
  private var statsSinks = listOf<String>()
  private var enablePlatformCertificatesValidation = false
  private var rtdsLayerName: String = ""
  private var rtdsTimeoutSeconds: Int = 0
  private var adsAddress: String = ""
  private var adsPort: Int = 0
  private var adsJwtToken: String = ""
  private var adsJwtTokenLifetimeSeconds: Int = 0
  private var adsSslRootCerts: String = ""
  private var nodeId: String = ""
  private var nodeRegion: String = ""
  private var nodeZone: String = ""
  private var nodeSubZone: String = ""
  private var cdsResourcesLocator: String = ""
  private var cdsTimeoutSeconds: Int = 0
  private var enableCds: Boolean = false

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
   * Specifies the domain (e.g. `example.com`) to use in the default gRPC stat sink to flush
   * stats.
   *
   * Setting this value enables the gRPC stat sink, which periodically flushes stats via the gRPC
   * MetricsService API. The flush interval is specified via addStatsFlushSeconds.
   *
   * @param grpcStatsDomain The domain to use for the gRPC stats sink.
   *
   * @return this builder.
   */
  fun addGrpcStatsDomain(grpcStatsDomain: String?): EngineBuilder {
    this.grpcStatsDomain = grpcStatsDomain
    return this
  }

  /**
   * Adds additional stats sinks, in the form of the raw YAML/JSON configuration.
   * Sinks added in this fashion will be included in addition to the gRPC stats sink
   * that may be enabled via addGrpcStatsDomain.
   *
   * @param statsSinks Configurations of stat sinks to add.
   *
   * @return this builder.
   */
  fun addStatsSinks(statsSinks: List<String>): EngineBuilder {
    this.statsSinks = statsSinks
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
  fun addDNSPreresolveHostnames(dnsPreresolveHostnames: List<String>): EngineBuilder {
    this.dnsPreresolveHostnames = dnsPreresolveHostnames
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
   * Specify whether to enable DNS cache.
   *
   * Note that DNS cache requires an addition of a key value store named
   * 'reserved.platform_store'.
   *
   * @param enableDNSCache whether to enable DNS cache. Disabled by default.
   * @param saveInterval   the interval at which to save results to the configured key value store.
   *
   * @return This builder.
   */
  fun enableDNSCache(enableDNSCache: Boolean, saveInterval: Int = 1): EngineBuilder {
    this.enableDNSCache = enableDNSCache
    this.dnsCacheSaveIntervalSeconds = saveInterval
    return this
  }

  /**
   * Specify whether to use Happy Eyeballs when multiple IP stacks may be supported. Defaults to
   * true.
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
   * Specify whether to do gzip response decompression or not.  Defaults to true.
   *
   * @param enableGzipDecompression whether or not to gunzip responses.
   *
   * @return This builder.
   */
  fun enableGzipDecompression(enableGzipDecompression: Boolean): EngineBuilder {
    this.enableGzipDecompression = enableGzipDecompression
    return this
  }

  /**
   * Specify whether to do brotli response decompression or not.  Defaults to false.
   *
   * @param enableBrotliDecompression whether or not to brotli decompress responses.
   *
   * @return This builder.
   */
  fun enableBrotliDecompression(enableBrotliDecompression: Boolean): EngineBuilder {
    this.enableBrotliDecompression = enableBrotliDecompression
    return this
  }

  /**
   * Specify whether to support socket tagging or not. Defaults to false.
   *
   * @param enableSocketTagging whether or not support socket tagging.
   *
   * @return This builder.
   */
  fun enableSocketTagging(enableSocketTagging: Boolean): EngineBuilder {
    this.enableSocketTagging = enableSocketTagging
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
   * Specify whether system proxy settings should be respected. If yes, Envoy Mobile will
   * use Android APIs to query Android Proxy settings configured on a device and will
   * respect these settings when establishing connections with remote services.
   *
   * The method is introduced for experimentation purposes and as a safety guard against
   * critical issues in the implementation of the proxying feature. It's intended to be removed
   * after it's confirmed that proxies on Android work as expected.
   *
   * @param enableProxying whether to enable Envoy's support for proxies.
   *
   * @return This builder.
   */
  fun enableProxying(enableProxying: Boolean): EngineBuilder {
    this.enableProxying = enableProxying
    return this
  }

  /**
   * Allows Envoy to avoid having to wait on DNS response in the dynamic forward proxy filter
   * for requests that are proxied i.e., a proxied request that goes to example.com will
   * not have to wait for the DNS resolution for example.com domain if skipping of the DNS lookup
   * is enabled. Defaults to false.
   *
   * @param enableSkipDNSLookup whether to ship waiting for DNS responses in the
   *                            dynamic forward proxy filter for proxied requests.
   *
   * @return This builder.
   */
  fun enableSkipDNSLookupForProxiedRequests(enableSkipDNSLookup: Boolean): EngineBuilder {
    this.enableSkipDNSLookupForProxiedRequests = enableSkipDNSLookup
    return this
  }

  /**
   * Add a rate at which to ping h2 connections on new stream creation if the connection has
   * sat idle. Defaults to 1 millisecond which effectively enables h2 ping functionality
   * and results in a connection ping on every new stream creation. Set it to
   * 100000000 milliseconds to effectively disable the ping.
   *
   * @param idleIntervalMs rate in milliseconds.
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
   * @param timeoutSeconds rate in seconds to timeout h2 pings.
   *
   * @return this builder.
   */
  fun addH2ConnectionKeepaliveTimeoutSeconds(timeoutSeconds: Int): EngineBuilder {
    this.h2ConnectionKeepaliveTimeoutSeconds = timeoutSeconds
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
   * Register a key-value store implementation for internal use.
   *
   * @param name the name of the KV store.
   * @param keyValueStore the KV store implementation.
   *
   * @return this builder.
   */
  fun addKeyValueStore(name: String, keyValueStore: KeyValueStore): EngineBuilder {
    this.keyValueStores.put(name, keyValueStore)
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
   * @param cluster the JSON configuration string for a virtual cluster.
   *
   * @return this builder.
   */
  fun addVirtualCluster(cluster: String): EngineBuilder {
    this.virtualClustersLegacy.add(cluster)
    return this
  }

  /**
   * Add virtual cluster configurations.
   *
   * @param configs structured configurations of virtual clusters.
   *
   * @return this builder.
   */
  fun addVirtualClusters(configs: List<VirtualClusterConfig>): EngineBuilder {
    this.virtualClusters + configs;
    return this
  }

  /**
   * Sets the node.id field in the Bootstrap configuration.
   *
   * @param nodeId the node ID.
   *
   * @return this builder.
   */
  fun setNodeId(nodeId: String): EngineBuilder {
    this.nodeId = nodeId
    return this
  }

  /**
   * Sets the node.locality field in the Bootstrap configuration.
   *
   * @param region the region of the node locality.
   * @param zone the zone of the node locality.
   * @param subZone the sub-zone of the node locality.
   *
   * @return this builder.
   */
  fun setNodeLocality(region: String, zone: String, subZone: String): EngineBuilder {
    this.nodeRegion = region
    this.nodeZone = zone
    this.nodeSubZone = subZone
    return this
  }

  /**
  * Adds an ADS layer.
  * Note that only the state-of-the-world gRPC protocol is supported, not Delta gRPC.
  *
  * @param address the network address of the server.
  *
  * @param port the port of the server.
  *
  * @param jwtToken the JWT token.
  *
  * @param jwtTokenLifetimeSeconds the lifetime of the JWT token. If zero,
  *                                a default value is set in engine_builder.h.
  *
  * @param sslRootCerts the SSL root certificates.
  *
  * @return this builder.
  */
  fun setAggregatedDiscoveryService(
    address: String,
    port: Int,
    jwtToken: String = "",
    jwtTokenLifetimeSeconds: Int = 0,
    sslRootCerts: String = ""
  ): EngineBuilder {
    this.adsAddress = address
    this.adsPort = port
    this.adsJwtToken = jwtToken
    this.adsJwtTokenLifetimeSeconds = jwtTokenLifetimeSeconds
    this.adsSslRootCerts = sslRootCerts
    return this
  }

  /**
  * Adds a CDS layer.
  *
  * @param resourcesLocator The xdstp resource URI for fetching clusters.
  *                         If empty, xdstp is not used and a wildcard is inferred.
  *
  * @param timeoutSeconds The timeout in seconds. If zero, a default value is
  *                       set in engine_builder.h.
  *
  * @return this builder.
  */
  fun addCdsLayer(
    resourcesLocator: String = "",
    timeoutSeconds: Int = 0,
  ): EngineBuilder {
    this.cdsResourcesLocator = resourcesLocator
    this.cdsTimeoutSeconds = timeoutSeconds
    this.enableCds = true
    return this
  }


  /**
  * Adds an RTDS layer to default config. Requires that ADS be configured.
  *
  * @param layerName the layer name.
  *
  * @param timeoutSeconds The timeout in seconds. If zero, a default value is
  *                       set in engine_builder.h.
  *
  * @return this builder.
  */
  fun addRtdsLayer(layerName: String, timeoutSeconds: Int = 0): EngineBuilder {
    this.rtdsLayerName = layerName
    this.rtdsTimeoutSeconds = timeoutSeconds
    return this
  }

  /**
   * Set a runtime guard with the provided value.
   *
   * @param name the name of the runtime guard, e.g. test_feature_false.
   * @param value the value for the runtime guard.
   *
   * @return This builder.
   */
  fun setRuntimeGuard(name: String, value: Boolean): EngineBuilder {
    this.runtimeGuards.put(name, value)
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
      connectTimeoutSeconds,
      dnsRefreshSeconds,
      dnsFailureRefreshSecondsBase,
      dnsFailureRefreshSecondsMax,
      dnsQueryTimeoutSeconds,
      dnsMinRefreshSeconds,
      dnsPreresolveHostnames,
      enableDNSCache,
      dnsCacheSaveIntervalSeconds,
      enableDrainPostDnsRefresh,
      enableHttp3,
      enableGzipDecompression,
      enableBrotliDecompression,
      enableSocketTagging,
      enableHappyEyeballs,
      enableInterfaceBinding,
      h2ConnectionKeepaliveIdleIntervalMilliseconds,
      h2ConnectionKeepaliveTimeoutSeconds,
      maxConnectionsPerHost,
      statsFlushSeconds,
      streamIdleTimeoutSeconds,
      perTryIdleTimeoutSeconds,
      appVersion,
      appId,
      trustChainVerification,
      virtualClustersLegacy,
      virtualClusters,
      nativeFilterChain,
      platformFilterChain,
      stringAccessors,
      keyValueStores,
      statsSinks,
      runtimeGuards,
      enableSkipDNSLookupForProxiedRequests,
      enablePlatformCertificatesValidation,
      rtdsLayerName,
      rtdsTimeoutSeconds,
      adsAddress,
      adsPort,
      adsJwtToken,
      adsJwtTokenLifetimeSeconds,
      adsSslRootCerts,
      nodeId,
      nodeRegion,
      nodeZone,
      nodeSubZone,
      cdsResourcesLocator,
      cdsTimeoutSeconds,
      enableCds,
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

  /**
   * Specify whether to use platform provided certificate validation APIs or Envoy built-in
   * validation logic. Defaults to false.
   *
   * @param enablePlatformCertificatesValidation true if using platform APIs is desired.
   *
   * @return This builder.
   */
  fun enablePlatformCertificatesValidation(enablePlatformCertificatesValidation: Boolean):
    EngineBuilder {
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation
    return this
  }
}

package io.envoyproxy.envoymobile.engine

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilter
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterCallbacks
import java.nio.ByteBuffer
import org.assertj.core.api.Assertions.assertThat
import org.junit.Assert.fail
import org.junit.Test
import java.util.regex.Pattern

class TestFilter : EnvoyHTTPFilter {

override fun onRequestHeaders(headers: MutableMap<String, MutableList<String>>, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onRequestData(data: ByteBuffer, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onRequestTrailers(trailers: MutableMap<String, MutableList<String>>, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onResponseHeaders(headers: MutableMap<String, MutableList<String>>, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onResponseData(data: ByteBuffer, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onResponseTrailers(trailers: MutableMap<String, MutableList<String>>, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun setRequestFilterCallbacks(callbacks: EnvoyHTTPFilterCallbacks) {
}
override fun setResponseFilterCallbacks(callbacks: EnvoyHTTPFilterCallbacks) {
}
override fun onCancel( streamIntel: EnvoyStreamIntel, finalStreamIntel: EnvoyFinalStreamIntel) {
}
override fun onComplete( streamIntel: EnvoyStreamIntel, finalStreamIntel: EnvoyFinalStreamIntel) {
}
override fun onError(errorCode: Int, message: String, attemptCount: Int, streamIntel: EnvoyStreamIntel, finalStreamIntel: EnvoyFinalStreamIntel) {
}
override fun onResumeRequest(headers: MutableMap<String, MutableList<String>>, data: ByteBuffer, trailers: MutableMap<String, MutableList<String>>, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
override fun onResumeResponse(headers: MutableMap<String, MutableList<String>>, data: ByteBuffer, trailers: MutableMap<String, MutableList<String>>, endStream: Boolean, streamIntel: EnvoyStreamIntel): Array<Any> {
  return emptyArray()
}
}

class TestEnvoyHTTPFilterFactory(name : String) : EnvoyHTTPFilterFactory {
 private var filterName = name
 override fun getFilterName(): String {
   return filterName
 }

 override fun create(): EnvoyHTTPFilter {
   return TestFilter()
 }
}

class EnvoyConfigurationTest {

  fun buildTestEnvoyConfiguration(
    adminInterfaceEnabled: Boolean = false,
    grpcStatsDomain: String = "stats.example.com",
    connectTimeoutSeconds: Int = 123,
    dnsRefreshSeconds: Int = 234,
    dnsFailureRefreshSecondsBase: Int = 345,
    dnsFailureRefreshSecondsMax: Int = 456,
    dnsQueryTimeoutSeconds: Int = 321,
    dnsMinRefreshSeconds: Int = 12,
    dnsPreresolveHostnames: MutableList<String> = mutableListOf("hostname1", "hostname2"),
    enableDNSCache: Boolean = false,
    dnsCacheSaveIntervalSeconds: Int = 101,
    enableDrainPostDnsRefresh: Boolean = false,
    enableHttp3: Boolean = true,
    enableGzipDecompression: Boolean = true,
    enableGzipCompression: Boolean = false,
    enableBrotliDecompression: Boolean = false,
    enableBrotliCompression: Boolean = false,
    enableSocketTagging: Boolean = false,
    enableHappyEyeballs: Boolean = false,
    enableInterfaceBinding: Boolean = false,
    h2ConnectionKeepaliveIdleIntervalMilliseconds: Int = 222,
    h2ConnectionKeepaliveTimeoutSeconds: Int = 333,
    maxConnectionsPerHost: Int = 543,
    statsFlushSeconds: Int = 567,
    streamIdleTimeoutSeconds: Int = 678,
    perTryIdleTimeoutSeconds: Int = 910,
    appVersion: String = "v1.2.3",
    appId: String = "com.example.myapp",
    trustChainVerification: TrustChainVerification = TrustChainVerification.VERIFY_TRUST_CHAIN,
    virtualClusters: MutableList<String> = mutableListOf("{name: test1}", "{name: test2}"),
    filterChain: MutableList<EnvoyNativeFilterConfig> = mutableListOf(EnvoyNativeFilterConfig("buffer_filter_1", "{'@type': 'type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer'}"), EnvoyNativeFilterConfig("buffer_filter_2", "{'@type': 'type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer'}")),
    platformFilterFactories: MutableList<EnvoyHTTPFilterFactory> = mutableListOf(TestEnvoyHTTPFilterFactory("name1"), TestEnvoyHTTPFilterFactory("name2")),
    enableSkipDNSLookupForProxiedRequests: Boolean = false,
    statSinks: MutableList<String> = mutableListOf(),
    enablePlatformCertificatesValidation: Boolean = false
  ): EnvoyConfiguration {
    return EnvoyConfiguration(
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
      enableGzipCompression,
      enableBrotliDecompression,
      enableBrotliCompression,
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
      virtualClusters,
      filterChain,
      platformFilterFactories,
      emptyMap(),
      emptyMap(),
      statSinks,
      enableSkipDNSLookupForProxiedRequests,
      enablePlatformCertificatesValidation
    )
  }

  @Test
  fun `configuration default values`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration()

    val resolvedTemplate = envoyConfiguration.createYaml()
    assertThat(resolvedTemplate).contains("&connect_timeout 123s")

    assertThat(resolvedTemplate).doesNotContain("admin: *admin_interface")

    // DNS
    assertThat(resolvedTemplate).contains("&dns_refresh_rate 234s")
    assertThat(resolvedTemplate).contains("&dns_fail_base_interval 345s")
    assertThat(resolvedTemplate).contains("&dns_fail_max_interval 456s")
    assertThat(resolvedTemplate).contains("&dns_query_timeout 321s")
    assertThat(resolvedTemplate).contains("&dns_lookup_family V4_PREFERRED")
    assertThat(resolvedTemplate).contains("&dns_min_refresh_rate 12s")
    assertThat(resolvedTemplate).contains("&dns_preresolve_hostnames [{address: hostname1, port_value: 443},{address: hostname2, port_value: 443}]")
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh false")

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding false")

    // Forcing IPv6
    assertThat(resolvedTemplate).contains("&force_ipv6 true")

    // H2 Ping
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_idle_interval 0.222s")
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_timeout 333s")

    // H3
    assertThat(resolvedTemplate).contains("http3_protocol_options:");
    assertThat(resolvedTemplate).contains("name: alternate_protocols_cache");

    // Gzip
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip");

    // Brotli
    assertThat(resolvedTemplate).doesNotContain("type.googleapis.com/envoy.extensions.compression.brotli.decompressor.v3.Brotli");

    // Per Host Limits
    assertThat(resolvedTemplate).contains("&max_connections_per_host 543")

    // Metadata
    assertThat(resolvedTemplate).contains("os: Android")
    assertThat(resolvedTemplate).contains("app_version: v1.2.3")
    assertThat(resolvedTemplate).contains("app_id: com.example.myapp")

    assertThat(resolvedTemplate).contains("virtual_clusters [{name: test1},{name: test2}]")

    // Stats
    assertThat(resolvedTemplate).contains("&stats_domain stats.example.com")
    assertThat(resolvedTemplate).contains("&stats_flush_interval 567s")
    assertThat(resolvedTemplate).contains("stats.example.com");

    // Idle timeouts
    assertThat(resolvedTemplate).contains("&stream_idle_timeout 678s")
    assertThat(resolvedTemplate).contains("&per_try_idle_timeout 910s")

    // TlS Verification
    assertThat(resolvedTemplate).contains("&trust_chain_verification VERIFY_TRUST_CHAIN")

    // Filters
    assertThat(resolvedTemplate).contains("buffer_filter_1")
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer")

    // Cert Validation
    assertThat(resolvedTemplate).contains("trusted_ca:")

    // Proxying
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests false")

    // Validate ordering between filters and platform filters
    assertThat(resolvedTemplate).matches(Pattern.compile(".*name1.*name2.*buffer_filter_1.*buffer_filter_2.*", Pattern.DOTALL));
  }

  @Test
  fun `configuration resolves with alternate values`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      adminInterfaceEnabled = false,
      grpcStatsDomain = "",
      enableDrainPostDnsRefresh = true,
      enableDNSCache = true,
      dnsCacheSaveIntervalSeconds = 101,
      enableHappyEyeballs = true,
      enableHttp3 = false,
      enableGzipDecompression = false,
      enableGzipCompression = true,
      enableBrotliDecompression = true,
      enableBrotliCompression = true,
      enableSocketTagging = true,
      enableInterfaceBinding = true,
      enableSkipDNSLookupForProxiedRequests = true,
      enablePlatformCertificatesValidation = true,
      dnsPreresolveHostnames = mutableListOf(),
      virtualClusters = mutableListOf(),
      filterChain = mutableListOf(),
      statSinks = mutableListOf("{ name: envoy.stat_sinks.statsd, typed_config: { '@type': type.googleapis.com/envoy.config.metrics.v3.StatsdSink, address: { socket_address: { address: 127.0.0.1, port_value: 123 } } } }"),
      trustChainVerification = TrustChainVerification.ACCEPT_UNTRUSTED
    )

    val resolvedTemplate = envoyConfiguration.createYaml()

    // enableDrainPostDnsRefresh = true
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh true")

    // enableDNSCache = true
    assertThat(resolvedTemplate).contains("key: dns_persistent_cache")
    // dnsCacheSaveIntervalSeconds = 101
    assertThat(resolvedTemplate).contains("&persistent_dns_cache_save_interval 101")

    // enableHappyEyeballs = true
    assertThat(resolvedTemplate).contains("&dns_lookup_family ALL")

    // enableHttp3 = false
    assertThat(resolvedTemplate).doesNotContain("name: alternate_protocols_cache");

    // enableGzipDecompression = false
    assertThat(resolvedTemplate).doesNotContain("type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip");

    // enableGzipCompression = true
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip");

    // enableBrotliDecompression = true
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.brotli.decompressor.v3.Brotli");

    // enableBrotliCompression = true
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.brotli.compressor.v3.Brotli");

    // enableInterfaceBinding = true
    assertThat(resolvedTemplate).contains("&enable_interface_binding true")

    // enableSkipDNSLookupForProxiedRequests = true
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests true")

    // enablePlatformCertificatesValidation = true
    assertThat(resolvedTemplate).doesNotContain("trusted_ca:")

    // statsSinks
    assertThat(resolvedTemplate).contains("envoy.stat_sinks.statsd");
  }

  @Test
  fun `test YAML loads with stats sinks and stats domain`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      grpcStatsDomain = "stats.example.com",
      statSinks = mutableListOf("{ name: envoy.stat_sinks.statsd, typed_config: { '@type': type.googleapis.com/envoy.config.metrics.v3.StatsdSink, address: { socket_address: { address: 127.0.0.1, port_value: 123 } } } }"),
      trustChainVerification = TrustChainVerification.ACCEPT_UNTRUSTED
    )

    val resolvedTemplate = envoyConfiguration.createYaml()

    // statsSinks
    assertThat(resolvedTemplate).contains("envoy.stat_sinks.statsd");
    assertThat(resolvedTemplate).contains("stats.example.com");
  }
}

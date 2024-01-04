package io.envoyproxy.envoymobile.engine

import com.google.protobuf.Struct
import com.google.protobuf.Value
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilter
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterCallbacks
import io.envoyproxy.envoymobile.engine.testing.TestJni
import java.nio.ByteBuffer
import org.assertj.core.api.Assertions.assertThat
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
    http3ConnectionOptions: String = "5RTO",
    http3ClientConnectionOptions: String = "MPQC",
    quicHints: Map<String, Int> = mapOf("www.abc.com" to 443, "www.def.com" to 443),
    quicCanonicalSuffixes: MutableList<String> = mutableListOf(".opq.com", ".xyz.com"),
    enableGzipDecompression: Boolean = true,
    enableBrotliDecompression: Boolean = false,
    enableSocketTagging: Boolean = false,
    enableInterfaceBinding: Boolean = false,
    h2ConnectionKeepaliveIdleIntervalMilliseconds: Int = 222,
    h2ConnectionKeepaliveTimeoutSeconds: Int = 333,
    maxConnectionsPerHost: Int = 543,
    streamIdleTimeoutSeconds: Int = 678,
    perTryIdleTimeoutSeconds: Int = 910,
    appVersion: String = "v1.2.3",
    appId: String = "com.example.myapp",
    trustChainVerification: TrustChainVerification = TrustChainVerification.VERIFY_TRUST_CHAIN,
    filterChain: MutableList<EnvoyNativeFilterConfig> = mutableListOf(EnvoyNativeFilterConfig("buffer_filter_1", "{'@type': 'type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer'}"), EnvoyNativeFilterConfig("buffer_filter_2", "{'@type': 'type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer'}")),
    platformFilterFactories: MutableList<EnvoyHTTPFilterFactory> = mutableListOf(TestEnvoyHTTPFilterFactory("name1"), TestEnvoyHTTPFilterFactory("name2")),
    runtimeGuards: Map<String,Boolean> = emptyMap(),
    enablePlatformCertificatesValidation: Boolean = false,
    rtdsResourceName: String = "",
    rtdsTimeoutSeconds: Int = 0,
    xdsAddress: String = "",
    xdsPort: Int = 0,
    xdsGrpcInitialMetadata: Map<String, String> = emptyMap(),
    xdsSslRootCerts: String = "",
    nodeId: String = "",
    nodeRegion: String = "",
    nodeZone: String = "",
    nodeSubZone: String = "",
    nodeMetadata: Struct = Struct.getDefaultInstance(),
    cdsResourcesLocator: String = "",
    cdsTimeoutSeconds: Int = 0,
    enableCds: Boolean = false,

  ): EnvoyConfiguration {
    return EnvoyConfiguration(
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
      http3ConnectionOptions,
      http3ClientConnectionOptions,
      quicHints,
      quicCanonicalSuffixes,
      enableGzipDecompression,
      enableBrotliDecompression,
      enableSocketTagging,
      enableInterfaceBinding,
      h2ConnectionKeepaliveIdleIntervalMilliseconds,
      h2ConnectionKeepaliveTimeoutSeconds,
      maxConnectionsPerHost,
      streamIdleTimeoutSeconds,
      perTryIdleTimeoutSeconds,
      appVersion,
      appId,
      trustChainVerification,
      filterChain,
      platformFilterFactories,
      emptyMap(),
      emptyMap(),
      runtimeGuards,
      enablePlatformCertificatesValidation,
      rtdsResourceName,
      rtdsTimeoutSeconds,
      xdsAddress,
      xdsPort,
      xdsGrpcInitialMetadata,
      xdsSslRootCerts,
      nodeId,
      nodeRegion,
      nodeZone,
      nodeSubZone,
      nodeMetadata,
      cdsResourcesLocator,
      cdsTimeoutSeconds,
      enableCds
    )
  }

  fun isEnvoyMobileXdsDisabled(): Boolean {
    return System.getProperty("envoy_jni_envoy_mobile_xds_disabled") != null;
  }

  @Test
  fun `configuration default values`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration()

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)
    assertThat(resolvedTemplate).contains("connect_timeout: 123s")

    // DNS
    assertThat(resolvedTemplate).contains("dns_refresh_rate: 234s")
    assertThat(resolvedTemplate).contains("base_interval: 345s")
    assertThat(resolvedTemplate).contains("max_interval: 456s")
    assertThat(resolvedTemplate).contains("dns_query_timeout: 321s")
    assertThat(resolvedTemplate).contains("dns_lookup_family: ALL")
    assertThat(resolvedTemplate).contains("dns_min_refresh_rate: 12s")
    assertThat(resolvedTemplate).contains("preresolve_hostnames:")
    assertThat(resolvedTemplate).contains("hostname1")
    assertThat(resolvedTemplate).contains("hostname1")

    // Forcing IPv6
    assertThat(resolvedTemplate).contains("always_use_v6: true")

    // H2 Ping
    assertThat(resolvedTemplate).contains("connection_idle_interval: 0.222s")
    assertThat(resolvedTemplate).contains("timeout: 333s")

    // H3
    assertThat(resolvedTemplate).contains("http3_protocol_options:");
    assertThat(resolvedTemplate).contains("name: alternate_protocols_cache");
    assertThat(resolvedTemplate).contains("hostname: www.abc.com");
    assertThat(resolvedTemplate).contains("hostname: www.def.com");
    assertThat(resolvedTemplate).contains("port: 443");
    assertThat(resolvedTemplate).contains("canonical_suffixes:");
    assertThat(resolvedTemplate).contains(".opq.com");
    assertThat(resolvedTemplate).contains(".xyz.com");
    assertThat(resolvedTemplate).contains("connection_options: 5RTO");
    assertThat(resolvedTemplate).contains("client_connection_options: MPQC");

    // Per Host Limits
    assertThat(resolvedTemplate).contains("max_connections: 543")

    // Metadata
    assertThat(resolvedTemplate).contains("os: Android")
    assertThat(resolvedTemplate).contains("app_version: v1.2.3")
    assertThat(resolvedTemplate).contains("app_id: com.example.myapp")

    // Idle timeouts
    assertThat(resolvedTemplate).contains("stream_idle_timeout: 678s")
    assertThat(resolvedTemplate).contains("per_try_idle_timeout: 910s")

    // Filters
    assertThat(resolvedTemplate).contains("buffer_filter_1")
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer")

    // Cert Validation
    assertThat(resolvedTemplate).contains("trusted_ca:")

    // Validate ordering between filters and platform filters
    assertThat(resolvedTemplate).matches(Pattern.compile(".*name1.*name2.*buffer_filter_1.*buffer_filter_2.*", Pattern.DOTALL));
    // Validate that createYaml doesn't change filter order.
    val resolvedTemplate2 = TestJni.createYaml(envoyConfiguration)
    assertThat(resolvedTemplate2).matches(Pattern.compile(".*name1.*name2.*buffer_filter_1.*buffer_filter_2.*", Pattern.DOTALL));
    // Validate that createBootstrap also doesn't change filter order.
    // This may leak memory as the boostrap isn't used.
    envoyConfiguration.createBootstrap()
    val resolvedTemplate3 = TestJni.createYaml(envoyConfiguration)
    assertThat(resolvedTemplate3).matches(Pattern.compile(".*name1.*name2.*buffer_filter_1.*buffer_filter_2.*", Pattern.DOTALL));
  }

  @Test
  fun `configuration resolves with alternate values`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      enableDrainPostDnsRefresh = true,
      enableDNSCache = true,
      dnsCacheSaveIntervalSeconds = 101,
      enableHttp3 = false,
      enableGzipDecompression = false,
      enableBrotliDecompression = true,
      enableSocketTagging = true,
      enableInterfaceBinding = true,
      enablePlatformCertificatesValidation = true,
      dnsPreresolveHostnames = mutableListOf(),
      filterChain = mutableListOf(),
      runtimeGuards = mapOf("test_feature_false" to true),
      trustChainVerification = TrustChainVerification.ACCEPT_UNTRUSTED
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    // TlS Verification
    assertThat(resolvedTemplate).contains("trust_chain_verification: ACCEPT_UNTRUSTED")

    // enableDrainPostDnsRefresh = true
    assertThat(resolvedTemplate).contains("enable_drain_post_dns_refresh: true")

    // enableDNSCache = true
    assertThat(resolvedTemplate).contains("key: dns_persistent_cache")
    // dnsCacheSaveIntervalSeconds = 101
    assertThat(resolvedTemplate).contains("save_interval: 101")

    // enableHttp3 = false
    assertThat(resolvedTemplate).doesNotContain("name: alternate_protocols_cache");

    // enableGzipDecompression = false
    assertThat(resolvedTemplate).doesNotContain("type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip");

    // enableBrotliDecompression = true
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.brotli.decompressor.v3.Brotli");

    // enableInterfaceBinding = true
    assertThat(resolvedTemplate).contains("enable_interface_binding: true")

    // enablePlatformCertificatesValidation = true
    assertThat(resolvedTemplate).doesNotContain("trusted_ca:")

    // ADS and RTDS not included by default
    assertThat(resolvedTemplate).doesNotContain("rtds_layer:");
    assertThat(resolvedTemplate).doesNotContain("ads_config:");
    assertThat(resolvedTemplate).doesNotContain("cds_config:");
  }

  @Test
  fun `test YAML loads with multiple entries`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      runtimeGuards = mapOf("test_feature_false" to true, "test_feature_true" to false),
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).contains("test_feature_false");
    assertThat(resolvedTemplate).contains("test_feature_true");
  }

  @Test
  fun `test adding RTDS`() {
    if (isEnvoyMobileXdsDisabled()) {
      return;
    }

    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      rtdsResourceName = "fake_rtds_layer", rtdsTimeoutSeconds = 5432, xdsAddress = "FAKE_ADDRESS", xdsPort = 0
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)
    assertThat(resolvedTemplate).contains("fake_rtds_layer");
    assertThat(resolvedTemplate).contains("FAKE_ADDRESS");
    assertThat(resolvedTemplate).contains("initial_fetch_timeout: 5432s");
  }

  @Test
  fun `test adding RTDS and CDS`() {
    if (isEnvoyMobileXdsDisabled()) {
      return;
    }

    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      cdsResourcesLocator = "FAKE_CDS_LOCATOR", cdsTimeoutSeconds = 356, xdsAddress = "FAKE_ADDRESS", xdsPort = 0, enableCds = true
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).contains("FAKE_CDS_LOCATOR");
    assertThat(resolvedTemplate).contains("FAKE_ADDRESS");
    assertThat(resolvedTemplate).contains("initial_fetch_timeout: 356s");
  }

  @Test
  fun `test not using enableCds`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      cdsResourcesLocator = "FAKE_CDS_LOCATOR", cdsTimeoutSeconds = 356, xdsAddress = "FAKE_ADDRESS", xdsPort = 0
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).doesNotContain("FAKE_CDS_LOCATOR");
    assertThat(resolvedTemplate).doesNotContain("initial_fetch_timeout: 356s");
  }

  @Test
  fun `test enableCds with default string`() {
    if (isEnvoyMobileXdsDisabled()) {
      return;
    }

    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      enableCds = true, xdsAddress = "FAKE_ADDRESS", xdsPort = 0
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).contains("cds_config:");
    assertThat(resolvedTemplate).contains("initial_fetch_timeout: 5s");
  }

  @Test
  fun `test RTDS default timeout`() {
    if (isEnvoyMobileXdsDisabled()) {
      return;
    }

    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      rtdsResourceName = "fake_rtds_layer", xdsAddress = "FAKE_ADDRESS", xdsPort = 0
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).contains("initial_fetch_timeout: 5s")
  }

  @Test
  fun `test node metadata`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      nodeMetadata = Struct.newBuilder()
        .putFields("metadata_field", Value.newBuilder().setStringValue("metadata_value").build())
        .build()
    )

    val resolvedTemplate = TestJni.createYaml(envoyConfiguration)

    assertThat(resolvedTemplate).contains("metadata_field")
    assertThat(resolvedTemplate).contains("metadata_value")
  }
}

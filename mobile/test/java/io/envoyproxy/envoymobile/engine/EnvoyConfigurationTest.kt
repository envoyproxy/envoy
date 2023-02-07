package io.envoyproxy.envoymobile.engine

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import org.assertj.core.api.Assertions.assertThat
import org.junit.Assert.fail
import org.junit.Test

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
    dnsPreresolveHostnames: String = "[hostname]",
    enableDNSCache: Boolean = false,
    enableDrainPostDnsRefresh: Boolean = false,
    enableHttp3: Boolean = true,
    enableGzip: Boolean = true,
    enableBrotli: Boolean = false,
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
    virtualClusters: String = "[test]",
    enableSkipDNSLookupForProxiedRequests: Boolean = false,
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
      enableDrainPostDnsRefresh,
      enableHttp3,
      enableGzip,
      enableBrotli,
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
      listOf(EnvoyNativeFilterConfig("filter_name", "test_config")),
      emptyList(),
      emptyMap(),
      emptyMap(),
      emptyList(),
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
    assertThat(resolvedTemplate).contains("&dns_preresolve_hostnames [hostname]")
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

    assertThat(resolvedTemplate).contains("&virtual_clusters [test]")

    // Stats
    assertThat(resolvedTemplate).contains("&stats_domain stats.example.com")
    assertThat(resolvedTemplate).contains("&stats_flush_interval 567s")

    // Idle timeouts
    assertThat(resolvedTemplate).contains("&stream_idle_timeout 678s")
    assertThat(resolvedTemplate).contains("&per_try_idle_timeout 910s")

    // TlS Verification
    assertThat(resolvedTemplate).contains("&trust_chain_verification VERIFY_TRUST_CHAIN")

    // Filters
    assertThat(resolvedTemplate).contains("filter_name")
    assertThat(resolvedTemplate).contains("test_config")

    // Cert Validation
    assertThat(resolvedTemplate).contains("trusted_ca:")

    // Proxying
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests false")
  }

  @Test
  fun `configuration resolves with alternate values`() {
    JniLibrary.loadTestLibrary()
    val envoyConfiguration = buildTestEnvoyConfiguration(
      adminInterfaceEnabled = true,
      enableDrainPostDnsRefresh = true,
      enableDNSCache = true,
      enableHappyEyeballs = true,
      enableHttp3 = false,
      enableGzip = false,
      enableBrotli = true,
      enableInterfaceBinding = true,
      enableSkipDNSLookupForProxiedRequests = true,
      enablePlatformCertificatesValidation = true
    )

    val resolvedTemplate = envoyConfiguration.createYaml()

    // adminInterfaceEnabled = true
    assertThat(resolvedTemplate).contains("admin: *admin_interface")

    // enableDrainPostDnsRefresh = true
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh true")

    // enableDNSCache = true
    assertThat(resolvedTemplate).contains("key: dns_persistent_cache")

    // enableHappyEyeballs = true
    assertThat(resolvedTemplate).contains("&dns_lookup_family ALL")

    // enableHttp3 = false
    assertThat(resolvedTemplate).doesNotContain("name: alternate_protocols_cache");

    // enableGzip = false
    assertThat(resolvedTemplate).doesNotContain("type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip");

    // enableBrotli = true
    assertThat(resolvedTemplate).contains("type.googleapis.com/envoy.extensions.compression.brotli.decompressor.v3.Brotli");

    // enableInterfaceBinding = true
    assertThat(resolvedTemplate).contains("&enable_interface_binding true")

    // enableSkipDNSLookupForProxiedRequests = true
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests true")

    // enablePlatformCertificatesValidation = true
    assertThat(resolvedTemplate).doesNotContain("trusted_ca:")
  }
}

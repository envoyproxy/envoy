package io.envoyproxy.envoymobile.engine

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import org.assertj.core.api.Assertions.assertThat
import org.junit.Assert.fail
import org.junit.Test

private const val TEST_CONFIG =
"""
fixture_template:
- name: mock
  filters:
#{custom_filters}
"""

private const val PLATFORM_FILTER_CONFIG =
"""
  - platform_filter_name: {{ platform_filter_name }}
"""

private const val NATIVE_FILTER_CONFIG =
"""
  - name: {{ native_filter_name }}
    typed_config: {{ native_filter_typed_config }}
"""

private const val APCF_INSERT =
"""
  - name: AlternateProtocolsCacheFilter
"""

private const val GZIP_INSERT =
"""
  - name: GzipFilter
"""

private const val BROTLI_INSERT =
"""
  - name: BrotliFilter
"""

private const val SOCKET_TAG_INSERT =
"""
  - name: SocketTag
"""

private const val CERT_VALIDATION_TEMPLATE =
"""
  custom_validator_config:
    name: "dumb_validator"
"""

private const val PERSISTENT_DNS_CACHE_INSERT =
"""
  config: persistent_dns_cache
"""

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
    enableHttp3: Boolean = false,
    enableGzip: Boolean = true,
    enableBrotli: Boolean = false,
    enableSocketTagging: Boolean = false,
    enableHappyEyeballs: Boolean = false,
    enableInterfaceBinding: Boolean = false,
    h2ConnectionKeepaliveIdleIntervalMilliseconds: Int = 222,
    h2ConnectionKeepaliveTimeoutSeconds: Int = 333,
    h2ExtendKeepaliveTimeout: Boolean = false,
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
      h2ExtendKeepaliveTimeout,
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
  fun `configuration resolves with values`() {
    val envoyConfiguration = buildTestEnvoyConfiguration()

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT, SOCKET_TAG_INSERT, PERSISTENT_DNS_CACHE_INSERT,
      CERT_VALIDATION_TEMPLATE
    )
    assertThat(resolvedTemplate).contains("&connect_timeout 123s")

    assertThat(resolvedTemplate).doesNotContain("admin: *admin_interface")

    // DNS
    assertThat(resolvedTemplate).contains("&dns_refresh_rate 234s")
    assertThat(resolvedTemplate).contains("&dns_fail_base_interval 345s")
    assertThat(resolvedTemplate).contains("&dns_fail_max_interval 456s")
    assertThat(resolvedTemplate).contains("&dns_query_timeout 321s")
    assertThat(resolvedTemplate).contains("&dns_lookup_family V4_PREFERRED")
    assertThat(resolvedTemplate).contains("&dns_multiple_addresses false")
    assertThat(resolvedTemplate).contains("&dns_min_refresh_rate 12s")
    assertThat(resolvedTemplate).contains("&dns_preresolve_hostnames [hostname]")
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh false")
    assertThat(resolvedTemplate).doesNotContain(PERSISTENT_DNS_CACHE_INSERT);

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding false")

    // Forcing IPv6
    assertThat(resolvedTemplate).contains("&force_ipv6 true")

    // H2 Ping
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_idle_interval 0.222s")
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_timeout 333s")

    // H3
    assertThat(resolvedTemplate).doesNotContain(APCF_INSERT);

    // Gzip
    assertThat(resolvedTemplate).contains(GZIP_INSERT);

    // Brotli
    assertThat(resolvedTemplate).doesNotContain(BROTLI_INSERT);

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
    assertThat(resolvedTemplate).contains("custom_validator_config")

    // Proxying
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests false")
  }

  @Test
  fun `configuration resolves with alternate values`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      enableDrainPostDnsRefresh = true,
      enableDNSCache = true,
      enableHappyEyeballs = true,
      enableHttp3 = true,
      enableGzip = false,
      enableBrotli = true,
      enableInterfaceBinding = true,
      h2ExtendKeepaliveTimeout = true,
      enableSkipDNSLookupForProxiedRequests = true,
      enablePlatformCertificatesValidation = true
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT, SOCKET_TAG_INSERT, PERSISTENT_DNS_CACHE_INSERT,
CERT_VALIDATION_TEMPLATE
    )

    // DNS
    assertThat(resolvedTemplate).contains("&dns_lookup_family ALL")
    assertThat(resolvedTemplate).contains("&dns_multiple_addresses true")
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh true")
    assertThat(resolvedTemplate).contains("config: persistent_dns_cache")

    // H2
    assertThat(resolvedTemplate).contains("&h2_delay_keepalive_timeout true")

    // H3
    assertThat(resolvedTemplate).contains(APCF_INSERT);

    // Gzip
    assertThat(resolvedTemplate).doesNotContain(GZIP_INSERT);

    // Brotli
    assertThat(resolvedTemplate).contains(BROTLI_INSERT);

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding true")

    // Cert Validation
    assertThat(resolvedTemplate).contains("custom_validator_config")

    // Proxying
    assertThat(resolvedTemplate).contains("&skip_dns_lookup_for_proxied_requests true")
  }

  @Test
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = buildTestEnvoyConfiguration()

    try {
      envoyConfiguration.resolveTemplate("{{ missing }}", "", "", "", "", "", "", "", "")
      fail("Unresolved configuration keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("missing")
    }
  }
}

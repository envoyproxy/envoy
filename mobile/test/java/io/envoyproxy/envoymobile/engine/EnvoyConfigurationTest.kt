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

class EnvoyConfigurationTest {

  fun buildTestEnvoyConfiguration(
    adminInterfaceEnabled: Boolean = false,
    grpcStatsDomain: String = "stats.example.com",
    statsDPort: Int? = null,
    connectTimeoutSeconds: Int = 123,
    dnsRefreshSeconds: Int = 234,
    dnsFailureRefreshSecondsBase: Int = 345,
    dnsFailureRefreshSecondsMax: Int = 456,
    dnsQueryTimeoutSeconds: Int = 321,
    dnsMinRefreshSeconds: Int = 12,
    dnsPreresolveHostnames: String = "[hostname]",
    dnsFallbackNameservers: List<String> = emptyList(),
    enableDnsFilterUnroutableFamilies: Boolean = true,
    dnsUseSystemResolver: Boolean = false,
    enableDrainPostDnsRefresh: Boolean = false,
    enableHttp3: Boolean = false,
    enableGzip: Boolean = true,
    enableBrotli: Boolean = false,
    enableHappyEyeballs: Boolean = false,
    enableInterfaceBinding: Boolean = false,
    forceIPv6: Boolean = false,
    h2ConnectionKeepaliveIdleIntervalMilliseconds: Int = 222,
    h2ConnectionKeepaliveTimeoutSeconds: Int = 333,
    h2ExtendKeepaliveTimeout: Boolean = false,
    h2RawDomains: List<String> = listOf("h2-raw.example.com"),
    maxConnectionsPerHost: Int = 543,
    statsFlushSeconds: Int = 567,
    streamIdleTimeoutSeconds: Int = 678,
    perTryIdleTimeoutSeconds: Int = 910,
    appVersion: String = "v1.2.3",
    appId: String = "com.example.myapp",
    trustChainVerification: TrustChainVerification = TrustChainVerification.VERIFY_TRUST_CHAIN,
    virtualClusters: String = "[test]"
  ): EnvoyConfiguration {
    return EnvoyConfiguration(
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
      enableDnsFilterUnroutableFamilies,
      dnsUseSystemResolver,
      enableDrainPostDnsRefresh,
      enableHttp3,
      enableGzip,
      enableBrotli,
      enableHappyEyeballs,
      enableInterfaceBinding,
      forceIPv6,
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
      listOf(EnvoyNativeFilterConfig("filter_name", "test_config")),
      emptyList(),
      emptyMap(),
      emptyMap()
    )
  }

  @Test
  fun `configuration resolves with values`() {
    val envoyConfiguration = buildTestEnvoyConfiguration()

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT

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
    assertThat(resolvedTemplate).contains("&dns_resolver_name envoy.network.dns_resolver.cares")
    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":[],\"use_resolvers_as_fallback\": false, \"filter_unroutable_families\": true}")
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh false")

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding false")

    // Forcing IPv6
    assertThat(resolvedTemplate).contains("&force_ipv6 false")

    // H2 Ping
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_idle_interval 0.222s")
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_timeout 333s")

    // H2 Hostnames
    assertThat(resolvedTemplate).contains("&h2_raw_domains [\"h2-raw.example.com\"]")

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
  }

  @Test
  fun `configuration resolves with alternate values`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      dnsFallbackNameservers = listOf("8.8.8.8"),
      enableDnsFilterUnroutableFamilies = false,
      enableDrainPostDnsRefresh = true,
      enableHappyEyeballs = true,
      enableHttp3 = true,
      enableGzip = false,
      enableBrotli = true,
      enableInterfaceBinding = true,
      forceIPv6 = true,
      h2ExtendKeepaliveTimeout = true
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT
    )

    // DNS
    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":[{\"socket_address\":{\"address\":\"8.8.8.8\"}}],\"use_resolvers_as_fallback\": true, \"filter_unroutable_families\": false}")
    assertThat(resolvedTemplate).contains("&dns_lookup_family ALL")
    assertThat(resolvedTemplate).contains("&dns_multiple_addresses true")
    assertThat(resolvedTemplate).contains("&enable_drain_post_dns_refresh true")

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

    // Forcing IPv6
    assertThat(resolvedTemplate).contains("&force_ipv6 true")
  }

  @Test
  fun `configuration resolves with system DNS resolver`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      dnsUseSystemResolver = true
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT
    )

    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig\"}")
    assertThat(resolvedTemplate).contains("&dns_resolver_name envoy.network.dns_resolver.getaddrinfo")
  }

  @Test
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = buildTestEnvoyConfiguration()

    try {
      envoyConfiguration.resolveTemplate("{{ missing }}", "", "", "", "", "")
      fail("Unresolved configuration keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("missing")
    }
  }

  @Test
  fun `cannot configure both statsD and gRPC stat sink`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      grpcStatsDomain = "stats.example.com",
      statsDPort = 5050
    )

    try {
      envoyConfiguration.resolveTemplate("", "", "", "", "", "")
      fail("Conflicting stats keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("cannot enable both statsD and gRPC metrics sink")
    }
  }

  @Test
  fun `resolving multiple h2 raw domains`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      h2RawDomains = listOf("h2-raw.example.com", "h2-raw.example.com2")
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT
    )

    assertThat(resolvedTemplate).contains("&h2_raw_domains [\"h2-raw.example.com\",\"h2-raw.example.com2\"]")
  }

  @Test
  fun `resolving multiple dns fallback nameservers`() {
    val envoyConfiguration = buildTestEnvoyConfiguration(
      dnsFallbackNameservers = listOf("8.8.8.8", "1.1.1.1")
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG, APCF_INSERT, GZIP_INSERT, BROTLI_INSERT
    )

    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":[{\"socket_address\":{\"address\":\"8.8.8.8\"}},{\"socket_address\":{\"address\":\"1.1.1.1\"}}],\"use_resolvers_as_fallback\": true, \"filter_unroutable_families\": true}")
  }
}

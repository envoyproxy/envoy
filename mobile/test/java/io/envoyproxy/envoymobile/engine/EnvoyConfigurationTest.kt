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

class EnvoyConfigurationTest {

  @Test
  fun `resolving with default configuration resolves with values`() {
    val envoyConfiguration = EnvoyConfiguration(
      false, "stats.foo.com", null, 123, 234, 345, 456, 321, "[hostname]", listOf("8.8.8.8"), true,
      true, true, 222, 333, listOf("h2-raw.domain"), 567, 678, 910, "v1.2.3", "com.mydomain.myapp",
      TrustChainVerification.ACCEPT_UNTRUSTED, "[test]",
      listOf(EnvoyNativeFilterConfig("filter_name", "test_config")), emptyList(), emptyMap()
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG
    )
    assertThat(resolvedTemplate).contains("&connect_timeout 123s")

    assertThat(resolvedTemplate).doesNotContain("admin: *admin_interface")

    // DNS
    assertThat(resolvedTemplate).contains("&dns_refresh_rate 234s")
    assertThat(resolvedTemplate).contains("&dns_fail_base_interval 345s")
    assertThat(resolvedTemplate).contains("&dns_fail_max_interval 456s")
    assertThat(resolvedTemplate).contains("&dns_query_timeout 321s")
    assertThat(resolvedTemplate).contains("&dns_lookup_family ALL")
    assertThat(resolvedTemplate).contains("&dns_multiple_addresses true")
    assertThat(resolvedTemplate).contains("&dns_preresolve_hostnames [hostname]")
    assertThat(resolvedTemplate).contains("&dns_resolver_name envoy.network.dns_resolver.cares")
    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":[{\"socket_address\":{\"address\":\"8.8.8.8\"}}],\"use_resolvers_as_fallback\": true, \"filter_unroutable_families\": true}")

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding true")

    // H2 Ping
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_idle_interval 0.222s")
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_timeout 333s")

    // H2 Hostnames
    assertThat(resolvedTemplate).contains("&h2_raw_domains [\"h2-raw.domain\"]")

    // Metadata
    assertThat(resolvedTemplate).contains("os: Android")
    assertThat(resolvedTemplate).contains("app_version: v1.2.3")
    assertThat(resolvedTemplate).contains("app_id: com.mydomain.myapp")

    assertThat(resolvedTemplate).contains("&virtual_clusters [test]")

    // Stats
    assertThat(resolvedTemplate).contains("&stats_domain stats.foo.com")
    assertThat(resolvedTemplate).contains("&stats_flush_interval 567s")

    // Idle timeouts
    assertThat(resolvedTemplate).contains("&stream_idle_timeout 678s")
    assertThat(resolvedTemplate).contains("&per_try_idle_timeout 910s")

    // TlS Verification
    assertThat(resolvedTemplate).contains("&trust_chain_verification ACCEPT_UNTRUSTED")

    // Filters
    assertThat(resolvedTemplate).contains("filter_name")
    assertThat(resolvedTemplate).contains("test_config")
  }

  @Test
  fun `resolving with alternate values also sets appropriate config`() {
    val envoyConfiguration = EnvoyConfiguration(
      false, "stats.foo.com", null, 123, 234, 345, 456, 321, "[hostname]", emptyList(), false,
      false, false, 222, 333, emptyList(), 567, 678, 910, "v1.2.3", "com.mydomain.myapp",
      TrustChainVerification.ACCEPT_UNTRUSTED, "[test]",
      listOf(EnvoyNativeFilterConfig("filter_name", "test_config")), emptyList(), emptyMap()
    )

    val resolvedTemplate = envoyConfiguration.resolveTemplate(
      TEST_CONFIG, PLATFORM_FILTER_CONFIG, NATIVE_FILTER_CONFIG
    )

    // DNS
    assertThat(resolvedTemplate).contains("&dns_resolver_config {\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":[],\"use_resolvers_as_fallback\": false, \"filter_unroutable_families\": false}")
    assertThat(resolvedTemplate).contains("&dns_lookup_family V4_PREFERRED")
    assertThat(resolvedTemplate).contains("&dns_multiple_addresses false")

    // Interface Binding
    assertThat(resolvedTemplate).contains("&enable_interface_binding false")
  }

  @Test
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = EnvoyConfiguration(
      false, "stats.foo.com", null, 123, 234, 345, 456, 321, "[hostname]", emptyList(), false,
      false, false, 123, 123, emptyList(), 567, 678, 910, "v1.2.3", "com.mydomain.myapp",
      TrustChainVerification.ACCEPT_UNTRUSTED, "[test]", emptyList(), emptyList(), emptyMap()
    )

    try {
      envoyConfiguration.resolveTemplate("{{ missing }}", "", "")
      fail("Unresolved configuration keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("missing")
    }
  }

  @Test
  fun `cannot configure both statsD and gRPC stat sink`() {
    val envoyConfiguration = EnvoyConfiguration(
      false, "stats.foo.com", 5050, 123, 234, 345, 456, 321, "[hostname]", emptyList(), false,
      false, false, 123, 123, emptyList(), 567, 678, 910, "v1.2.3", "com.mydomain.myapp",
      TrustChainVerification.ACCEPT_UNTRUSTED, "[test]", emptyList(), emptyList(), emptyMap()
    )

    try {
      envoyConfiguration.resolveTemplate("", "", "")
      fail("Conflicting stats keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("cannot enable both statsD and gRPC metrics sink")
    }
  }
}

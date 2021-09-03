package io.envoyproxy.envoymobile.engine

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
      false, "stats.foo.com", null, 123, 234, 345, 456, 321, "[hostname]", 222, 333, 567, 678, "v1.2.3", "com.mydomain.myapp", "[test]",
      listOf<EnvoyNativeFilterConfig>(EnvoyNativeFilterConfig("filter_name", "test_config")),
      emptyList(), emptyMap()
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
    assertThat(resolvedTemplate).contains("&dns_preresolve_hostnames [hostname]")

    // H2 Ping
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_idle_interval 0.222s")
    assertThat(resolvedTemplate).contains("&h2_connection_keepalive_timeout 333s")

    // Metadata
    assertThat(resolvedTemplate).contains("os: Android")
    assertThat(resolvedTemplate).contains("app_version: v1.2.3")
    assertThat(resolvedTemplate).contains("app_id: com.mydomain.myapp")

    assertThat(resolvedTemplate).contains("&virtual_clusters [test]")

    // Stats
    assertThat(resolvedTemplate).contains("&stats_domain stats.foo.com")
    assertThat(resolvedTemplate).contains("&stats_flush_interval 567s")

    // Filters
    assertThat(resolvedTemplate).contains("filter_name")
    assertThat(resolvedTemplate).contains("test_config")
  }

  @Test
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = EnvoyConfiguration(
      false, "stats.foo.com", null, 123, 234, 345, 456, 321, "[hostname]", 123, 123, 567, 678, "v1.2.3", "com.mydomain.myapp", "[test]",
      emptyList(), emptyList(), emptyMap()
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
      false, "stats.foo.com", 5050, 123, 234, 345, 456, 321, "[hostname]", 123, 123, 567, 678, "v1.2.3", "com.mydomain.myapp", "[test]",
      emptyList(), emptyList(), emptyMap()
    )

    try {
      envoyConfiguration.resolveTemplate("", "", "")
      fail("Conflicting stats keys should trigger exception.")
    } catch (e: EnvoyConfiguration.ConfigurationException) {
      assertThat(e.message).contains("cannot enable both statsD and gRPC metrics sink")
    }
  }
}

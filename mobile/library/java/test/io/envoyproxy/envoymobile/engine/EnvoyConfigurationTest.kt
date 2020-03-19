package io.envoyproxy.envoymobile.engine

import org.junit.Test
import org.assertj.core.api.Assertions.assertThat

private const val TEST_CONFIG = """
mock_template:
- name: mock
  stats_domain: {{ stats_domain }}
  connect_timeout: {{ connect_timeout_seconds }}s
  dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
  dns_failure_refresh_rate:
    base_interval: {{ dns_failure_refresh_rate_seconds_base }}s
    max_interval: {{ dns_failure_refresh_rate_seconds_max }}s
  stats_flush_interval: {{ stats_flush_interval_seconds }}s
  os: {{ device_os }}
  app_version: {{ app_version }}
  app_id: {{ app_id }}
"""


class EnvoyConfigurationTest {

  @Test
  fun `resolving with default configuration resolves with values`() {
    val envoyConfiguration = EnvoyConfiguration("stats.foo.com", 123, 234, 345, 456, 567, "v1.2.3", "com.mydomain.myapp")

    val resolvedTemplate = envoyConfiguration.resolveTemplate(TEST_CONFIG)
    assertThat(resolvedTemplate).contains("stats_domain: stats.foo.com")
    assertThat(resolvedTemplate).contains("connect_timeout: 123s")
    assertThat(resolvedTemplate).contains("dns_refresh_rate: 234s")
    assertThat(resolvedTemplate).contains("base_interval: 345s")
    assertThat(resolvedTemplate).contains("max_interval: 456s")
    assertThat(resolvedTemplate).contains("stats_flush_interval: 567s")
    assertThat(resolvedTemplate).contains("os: Android")
    assertThat(resolvedTemplate).contains("app_version: v1.2.3")
    assertThat(resolvedTemplate).contains("app_id: com.mydomain.myapp")
  }


  @Test(expected = EnvoyConfiguration.ConfigurationException::class)
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = EnvoyConfiguration("stats.foo.com", 123, 234, 345, 456, 567, "v1.2.3", "com.mydomain.myapp")

    envoyConfiguration.resolveTemplate("{{ }}")
  }
}

package io.envoyproxy.envoymobile.engine

import org.junit.Test
import org.assertj.core.api.Assertions.assertThat

private const val TEST_CONFIG = """
mock_template:
- name: mock
  domain: {{ domain }}
  connect_timeout: {{ connect_timeout }}
  dns_refresh_rate: {{ dns_refresh_rate }}
  stats_flush_interval: {{ stats_flush_interval }}
"""


class EnvoyConfigurationTest {

  @Test
  fun `resolving with default configuration resolves with values`() {
    val envoyConfiguration = EnvoyConfiguration("api.foo.com", 123, 234, 345)

    val resolvedTemplate = envoyConfiguration.resolveTemplate(TEST_CONFIG)
    assertThat(resolvedTemplate).contains("domain: api.foo.com")
    assertThat(resolvedTemplate).contains("connect_timeout: 123s")
    assertThat(resolvedTemplate).contains("dns_refresh_rate: 234s")
    assertThat(resolvedTemplate).contains("stats_flush_interval: 345s")
  }


  @Test(expected = EnvoyConfiguration.ConfigurationException::class)
  fun `resolve templates with invalid templates will throw on build`() {
    val envoyConfiguration = EnvoyConfiguration("api.foo.com", 123, 234, 345)

    envoyConfiguration.resolveTemplate("{{ }}")
  }
}

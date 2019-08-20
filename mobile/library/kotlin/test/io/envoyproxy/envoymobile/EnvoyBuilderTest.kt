package io.envoyproxy.envoymobile.io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.ConfigurationException
import io.envoyproxy.envoymobile.EnvoyBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.mockito.Mockito.`when`
import org.mockito.Mockito.mock

private const val TEST_CONFIG ="""
mock_template:
- name: mock
  connect_timeout: {{ connect_timeout }}
  dns_refresh_rate: {{ dns_refresh_rate }}
  stats_flush_interval: {{ stats_flush_interval }}
"""

class EnvoyBuilderTest {

  private lateinit var builder: EnvoyBuilder

  private var envoyConfiguration: EnvoyConfiguration = mock(EnvoyConfiguration::class.java)
  private var engine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Test
  fun `adding custom config builder uses custom config for running Envoy`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addConfigYAML("mock_template:")
    val envoy = builder.build()
    assertThat(envoy.config).isEqualTo("mock_template:")
  }

  @Test
  fun `adding log level builder uses log level for running Envoy`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addLogLevel(LogLevel.DEBUG)
    val envoy = builder.build()
    assertThat(envoy.logLevel).isEqualTo(LogLevel.DEBUG)
  }

  @Test
  fun `specifying connection timeout overrides default`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addConnectTimeoutSeconds(1234)
    val envoy = builder.build()
    assertThat(envoy.config).contains("connect_timeout: 1234s")
  }

  @Test
  fun `specifying DNS refresh overrides default`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addDNSRefreshSeconds(1234)
    val envoy = builder.build()
    assertThat(envoy.config).contains("dns_refresh_rate: 1234s")
  }

  @Test
  fun `specifying stats flush overrides default`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addStatsFlushSeconds(1234)
    builder.build()
    val envoy = builder.build()
    assertThat(envoy.config).contains("stats_flush_interval: 1234s")

  }

  @Test(expected = ConfigurationException::class)
  fun `specifying configs with invalid templates will throw on build`() {
    `when`(envoyConfiguration.templateString()).thenReturn(TEST_CONFIG)
    builder = EnvoyBuilder(envoyConfiguration)
    builder.addEngineType { engine }

    builder.addConfigYAML("{{ missing }}")
    builder.build()
  }
}

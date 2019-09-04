package io.envoyproxy.envoymobile.io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.EnvoyBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.mockito.Mockito.mock

private const val TEST_CONFIG = """
mock_template:
- name: mock
  connect_timeout: {{ connect_timeout }}
  dns_refresh_rate: {{ dns_refresh_rate }}
  stats_flush_interval: {{ stats_flush_interval }}
"""

class EnvoyBuilderTest {

  private lateinit var builder: EnvoyBuilder

  private var engine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Test
  fun `adding custom config builder uses custom config for running Envoy`() {
    builder = EnvoyBuilder()
    builder.addConfigYAML(TEST_CONFIG)
    builder.addEngineType { engine }

    builder.addConfigYAML("mock_template:")
    val envoy = builder.build()
    assertThat(envoy.configurationYAML).isEqualTo("mock_template:")
  }

  @Test
  fun `adding log level builder uses log level for running Envoy`() {
    builder = EnvoyBuilder()
    builder.addConfigYAML(TEST_CONFIG)
    builder.addEngineType { engine }

    builder.addLogLevel(LogLevel.DEBUG)
    val envoy = builder.build()
    assertThat(envoy.logLevel).isEqualTo(LogLevel.DEBUG)
  }

  @Test
  fun `specifying connection timeout overrides default`() {
    builder = EnvoyBuilder()
    builder.addEngineType { engine }

    builder.addConnectTimeoutSeconds(1234)
    val envoy = builder.build()
    assertThat(envoy.envoyConfiguration!!.connectTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS refresh overrides default`() {
    builder = EnvoyBuilder()
    builder.addEngineType { engine }

    builder.addDNSRefreshSeconds(1234)
    val envoy = builder.build()
    assertThat(envoy.envoyConfiguration!!.dnsRefreshSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying stats flush overrides default`() {
    builder = EnvoyBuilder()
    builder.addEngineType { engine }

    builder.addStatsFlushSeconds(1234)
    builder.build()
    val envoy = builder.build()
    assertThat(envoy.envoyConfiguration!!.statsFlushSeconds).isEqualTo(1234)

  }
}

package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.mockito.Mockito.mock

class StreamClientBuilderTest {
  private lateinit var clientBuilder: StreamClientBuilder
  private var engine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Test
  fun `adding log level builder uses log level for running Envoy`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addLogLevel(LogLevel.DEBUG)
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.logLevel).isEqualTo(LogLevel.DEBUG)
  }

  @Test
  fun `specifying stats domain overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addStatsDomain("stats.envoyproxy.io")
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.statsDomain).isEqualTo("stats.envoyproxy.io")
  }

  @Test
  fun `specifying connection timeout overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addConnectTimeoutSeconds(1234)
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.connectTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS refresh overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addDNSRefreshSeconds(1234)
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.dnsRefreshSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS failure refresh overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addDNSFailureRefreshSeconds(1234, 5678)
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.dnsFailureRefreshSecondsBase).isEqualTo(1234)
    assertThat(envoy.envoyConfiguration!!.dnsFailureRefreshSecondsMax).isEqualTo(5678)
  }

  @Test
  fun `specifying stats flush overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addStatsFlushSeconds(1234)
    clientBuilder.build()
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.statsFlushSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying app version overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addAppVersion("v1.2.3")
    clientBuilder.build()
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.appVersion).isEqualTo("v1.2.3")
  }

  @Test
  fun `specifying app id overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addAppId("com.envoymobile.android")
    clientBuilder.build()
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.appId).isEqualTo("com.envoymobile.android")
  }

  @Test
  fun `specifying virtual clusters overrides default`() {
    clientBuilder = StreamClientBuilder(Standard())
    clientBuilder.addEngineType { engine }

    clientBuilder.addVirtualClusters("[test]")
    clientBuilder.build()
    val envoy = clientBuilder.build() as EnvoyClient
    assertThat(envoy.envoyConfiguration!!.virtualClusters).isEqualTo("[test]")
  }
}

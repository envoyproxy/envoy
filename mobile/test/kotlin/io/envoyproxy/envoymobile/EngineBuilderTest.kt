package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.mockito.Mockito.mock

class EngineBuilderTest {
  private lateinit var engineBuilder: EngineBuilder
  private var envoyEngine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Test
  fun `adding log level builder uses log level for running Envoy`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addLogLevel(LogLevel.DEBUG)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.logLevel).isEqualTo(LogLevel.DEBUG)
  }

  @Test
  fun `specifying stats domain overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addGrpcStatsDomain("stats.envoyproxy.io")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.grpcStatsDomain).isEqualTo("stats.envoyproxy.io")
  }

  @Test
  fun `enabling admin interface overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.enableAdminInterface()

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.adminInterfaceEnabled).isTrue()
  }

  @Test
  fun `specifying connection timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addConnectTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.connectTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS refresh overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSRefreshSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.dnsRefreshSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS failure refresh overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSFailureRefreshSeconds(1234, 5678)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.dnsFailureRefreshSecondsBase).isEqualTo(1234)
    assertThat(engine.envoyConfiguration!!.dnsFailureRefreshSecondsMax).isEqualTo(5678)
  }

  @Test
  fun `specifying DNS query timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSQueryTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.dnsQueryTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying dns fallback nameservers overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSFallbackNameservers(listOf<String>("8.8.8.8"))

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.dnsFallbackNameservers.size).isEqualTo(1)
  }

  @Test
  fun `specifying H2 Ping idle interval overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addH2ConnectionKeepaliveIdleIntervalMilliseconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.h2ConnectionKeepaliveIdleIntervalMilliseconds).isEqualTo(1234)
  }

  @Test
  fun `specifying H2 Ping timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addH2ConnectionKeepaliveTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.h2ConnectionKeepaliveTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying stats flush overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addStatsFlushSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.statsFlushSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying stream idle timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addStreamIdleTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.streamIdleTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying per try idle timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addPerTryIdleTimeoutSeconds(5678)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.perTryIdleTimeoutSeconds).isEqualTo(5678)
  }

  @Test
  fun `specifying app version overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addAppVersion("v1.2.3")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.appVersion).isEqualTo("v1.2.3")
  }

  @Test
  fun `specifying app id overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addAppId("com.envoymobile.android")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.appId).isEqualTo("com.envoymobile.android")
  }

  @Test
  fun `specifying virtual clusters overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addVirtualClusters("[test]")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.virtualClusters).isEqualTo("[test]")
  }

  @Test
  fun `specifying native filters overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addNativeFilter("name", "config")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration!!.nativeFilterChain.size).isEqualTo(1)
  }
}

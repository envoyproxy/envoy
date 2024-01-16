package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.JniLibrary
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.mockito.Mockito.mock

class EngineBuilderTest {
  private lateinit var engineBuilder: EngineBuilder
  private var envoyEngine: EnvoyEngine = mock(EnvoyEngine::class.java)

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `adding log level builder uses log level for running Envoy`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addLogLevel(LogLevel.DEBUG)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.logLevel).isEqualTo(LogLevel.DEBUG)
  }

  @Test
  fun `enabling interface binding overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.enableInterfaceBinding(true)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.enableInterfaceBinding).isTrue()
  }

  @Test
  fun `specifying connection timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addConnectTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.connectTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying min DNS refresh overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSMinRefreshSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.dnsMinRefreshSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS refresh overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSRefreshSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.dnsRefreshSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying DNS failure refresh overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSFailureRefreshSeconds(1234, 5678)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.dnsFailureRefreshSecondsBase).isEqualTo(1234)
    assertThat(engine.envoyConfiguration.dnsFailureRefreshSecondsMax).isEqualTo(5678)
  }

  @Test
  fun `specifying DNS query timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addDNSQueryTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.dnsQueryTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `DNS cache is disabled by default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.enableDNSCache).isFalse()
  }

  @Test
  fun `enabling DNS cache overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.enableDNSCache(true)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.enableDNSCache).isTrue()
  }

  @Test
  fun `specifying H2 Ping idle interval overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addH2ConnectionKeepaliveIdleIntervalMilliseconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.h2ConnectionKeepaliveIdleIntervalMilliseconds)
      .isEqualTo(1234)
  }

  @Test
  fun `specifying H2 Ping timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addH2ConnectionKeepaliveTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.h2ConnectionKeepaliveTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying max connections per host overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.setMaxConnectionsPerHost(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.maxConnectionsPerHost).isEqualTo(1234)
  }

  @Test
  fun `specifying stream idle timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addStreamIdleTimeoutSeconds(1234)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.streamIdleTimeoutSeconds).isEqualTo(1234)
  }

  @Test
  fun `specifying per try idle timeout overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addPerTryIdleTimeoutSeconds(5678)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.perTryIdleTimeoutSeconds).isEqualTo(5678)
  }

  @Test
  fun `specifying app version overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addAppVersion("v1.2.3")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.appVersion).isEqualTo("v1.2.3")
  }

  @Test
  fun `specifying app id overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addAppId("com.envoymobile.android")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.appId).isEqualTo("com.envoymobile.android")
  }

  @Test
  fun `specifying native filters overrides default`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.addNativeFilter("name", "config")

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.nativeFilterChain.size).isEqualTo(1)
  }

  @Test
  fun `specifying xDS works`() {
    var xdsBuilder = XdsBuilder("fake_test_address", 0)
    xdsBuilder
      .addInitialStreamHeader("x-goog-api-key", "A1B2C3")
      .addInitialStreamHeader("x-android-package", "com.google.myapp")
    xdsBuilder.setSslRootCerts("my_root_certs")
    xdsBuilder.addRuntimeDiscoveryService("some_rtds_resource")
    xdsBuilder.addClusterDiscoveryService(
      "xdstp://fake_test_address/envoy.config.cluster.v3.Cluster/xyz"
    )
    engineBuilder = EngineBuilder(Standard())
    engineBuilder.addEngineType { envoyEngine }
    engineBuilder.setXds(xdsBuilder)

    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.xdsAddress).isEqualTo("fake_test_address")
    assertThat(engine.envoyConfiguration.xdsGrpcInitialMetadata)
      .isEqualTo(mapOf("x-goog-api-key" to "A1B2C3", "x-android-package" to "com.google.myapp"))
    assertThat(engine.envoyConfiguration.xdsRootCerts).isEqualTo("my_root_certs")
    assertThat(engine.envoyConfiguration.rtdsResourceName).isEqualTo("some_rtds_resource")
    assertThat(engine.envoyConfiguration.cdsResourcesLocator)
      .isEqualTo("xdstp://fake_test_address/envoy.config.cluster.v3.Cluster/xyz")
  }

  @Test
  fun `specifying runtime guards work`() {
    engineBuilder = EngineBuilder(Standard())
    engineBuilder
      .setRuntimeGuard("test_feature_false", true)
      .setRuntimeGuard("test_feature_true", false)
    val engine = engineBuilder.build() as EngineImpl
    assertThat(engine.envoyConfiguration.runtimeGuards["test_feature_false"]).isEqualTo("true")
    assertThat(engine.envoyConfiguration.runtimeGuards["test_feature_true"]).isEqualTo("false")
  }
}

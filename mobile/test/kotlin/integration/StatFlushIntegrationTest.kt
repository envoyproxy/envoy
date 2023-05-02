package test.kotlin.integration

import io.envoyproxy.envoymobile.Element
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.After
import org.junit.Ignore
import org.junit.Test

class StatFlushIntegrationTest {
  private var engine: Engine? = null

  init {
    JniLibrary.loadTestLibrary()
  }

  @After
  fun teardown() {
    engine?.terminate()
    engine = null
  }

  @Test
  fun `multiple stat sinks configured`() {
    val countDownLatch = CountDownLatch(1)
    engine = EngineBuilder()
      .addLogLevel(LogLevel.DEBUG)
      // Really high flush interval so it won't trigger during test execution.
      .addStatsFlushSeconds(100)
      .addStatsSinks(listOf(statsdSinkConfig(8125), statsdSinkConfig(5000)))
      .addGrpcStatsDomain("example.com")
      .setOnEngineRunning { countDownLatch.countDown() }
      .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()
  }

  @Test
  fun `flush flushes to stats sink`() {
    val countDownLatch = CountDownLatch(1)
    engine = EngineBuilder()
      .addLogLevel(LogLevel.DEBUG)
      // Really high flush interval so it won't trigger during test execution.
      .addStatsFlushSeconds(100)
      .addStatsSinks(listOf(statsdSinkConfig(8125), statsdSinkConfig(5000)))
      .setOnEngineRunning { countDownLatch.countDown() }
      .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()

    val statsdServer1 = TestStatsdServer()
    statsdServer1.runAsync(8125)

    val statsdServer2 = TestStatsdServer()
    statsdServer2.runAsync(5000)

    statsdServer1.setStatMatching { s -> s == "envoy.pulse.foo.bar:1|c" }
    statsdServer2.setStatMatching { s -> s == "envoy.pulse.foo.bar:1|c" }

    engine!!.pulseClient().counter(Element("foo"), Element("bar")).increment(1)
    engine!!.flushStats()

    statsdServer1.awaitStatMatching()
    statsdServer2.awaitStatMatching()
  }

  private fun statsdSinkConfig(port: Int): String {
return """{ name: envoy.stat_sinks.statsd,
      typed_config: {
        "@type": type.googleapis.com/envoy.config.metrics.v3.StatsdSink,
        address: { socket_address: { address: 127.0.0.1, port_value: $port } } } }"""
  }
}

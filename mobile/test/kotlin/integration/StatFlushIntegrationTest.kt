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
  fun `concurrent flushes with histograms`() {
    val countDownLatch = CountDownLatch(1)
    engine = EngineBuilder()
      .addLogLevel(LogLevel.DEBUG)
      .addStatsFlushSeconds(1)
      .setOnEngineRunning { countDownLatch.countDown() }
      .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()

    engine!!.pulseClient().distribution(Element("something")).recordValue(100)

    // Verify that we can issue multiple concurrent stat flushes. Because stat flushes
    // are async, running it multiple times in a row ends up executing them concurrently.
    repeat(100) {
      engine!!.flushStats()
    }
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

    engine!!.pulseClient().counter(Element("foo"), Element("bar")).increment(1)

    val statsdServer1 = TestStatsdServer()
    statsdServer1.runAsync(8125)

    val statsdServer2 = TestStatsdServer()
    statsdServer2.runAsync(5000)

    engine!!.flushStats()

    statsdServer1.awaitStatMatching { s -> s == "envoy.pulse.foo.bar:1|c" }
    statsdServer2.awaitStatMatching { s -> s == "envoy.pulse.foo.bar:1|c" }
  }

  private fun statsdSinkConfig(port: Int): String {
return """{ name: envoy.stat_sinks.statsd,
      typed_config: {
        "@type": type.googleapis.com/envoy.config.metrics.v3.StatsdSink,
        address: { socket_address: { address: 127.0.0.1, port_value: $port } } } }"""
  }
}

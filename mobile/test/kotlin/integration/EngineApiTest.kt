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

class EngineApiTest {
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
  fun `verify engine APIs`() {
    val countDownLatch = CountDownLatch(1)
    engine = EngineBuilder()
      .addLogLevel(LogLevel.INFO)
      .addStatsFlushSeconds(1)
      .setOnEngineRunning { countDownLatch.countDown() }
      .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()

    engine!!.pulseClient().counter(Element("foo"), Element("bar")).increment(1)

    // FIXME(jpsim): Fix crash that occurs when uncommenting this line
    // assertThat(engine?.dumpStats()).contains("pulse.foo.bar: 1")
  }
}

package test.kotlin.integration

import io.envoyproxy.envoymobile.Element
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class EngineApiTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `verify engine APIs`() {
    val countDownLatch = CountDownLatch(1)
    val engine = EngineBuilder()
      .addLogLevel(LogLevel.INFO)
      .addStatsFlushSeconds(1)
      .setOnEngineRunning { countDownLatch.countDown() }
      .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()

    engine.pulseClient().counter(Element("foo"), Element("bar")).increment(1)

    // FIXME(jpsim): Fix crash that occurs when uncommenting this line
    // assertThat(engine?.dumpStats()).contains("pulse.foo.bar: 1")

    engine.terminate()
  }
}

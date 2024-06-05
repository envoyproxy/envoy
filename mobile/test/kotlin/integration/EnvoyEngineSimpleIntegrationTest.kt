package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class EnvoyEngineSimpleIntegrationTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `ensure engine build and termination succeeds with no errors`() {
    val countDownLatch = CountDownLatch(1)
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setOnEngineRunning { countDownLatch.countDown() }
        .build()
    countDownLatch.await(5, TimeUnit.SECONDS)
    engine.terminate()
  }

  @Test
  fun `ensure no other operations can be executed after a termination`() {
    val countDownLatch = CountDownLatch(1)
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setOnEngineRunning { countDownLatch.countDown() }
        .build()
    countDownLatch.await(5, TimeUnit.SECONDS)
    engine.terminate()

    assertThrows(IllegalStateException::class.java) { engine.dumpStats() }
    assertThrows(IllegalStateException::class.java) { engine.resetConnectivityState() }
    assertThrows(IllegalStateException::class.java) { engine.terminate() }
  }
}

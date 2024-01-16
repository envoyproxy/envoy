package test.kotlin.integration

import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class SetLoggerTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `set logger`() {
    val countDownLatch = CountDownLatch(1)
    val logEventLatch = CountDownLatch(1)
    val engine =
      EngineBuilder(Standard())
        .addLogLevel(LogLevel.DEBUG)
        .addNativeFilter(
          "test_logger",
          "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger\"}"
        )
        .setLogger { msg ->
          if (msg.contains("starting main dispatch loop")) {
            countDownLatch.countDown()
          }
        }
        .setEventTracker { event ->
          if (event["log_name"] == "event_name") {
            logEventLatch.countDown()
          }
        }
        .build()

    countDownLatch.await(30, TimeUnit.SECONDS)

    sendRequest(engine)

    logEventLatch.await(30, TimeUnit.SECONDS)

    engine.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
    assertThat(logEventLatch.count).isEqualTo(0)
  }

  @Test
  fun `engine should continue to run if no logger is set`() {
    val countDownLatch = CountDownLatch(1)
    val logEventLatch = CountDownLatch(1)
    val engine =
      EngineBuilder(Standard())
        .setEventTracker { event ->
          if (event["log_name"] == "event_name") {
            logEventLatch.countDown()
          }
        }
        .addLogLevel(LogLevel.DEBUG)
        .addNativeFilter(
          "test_logger",
          "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger\"}"
        )
        .setOnEngineRunning { countDownLatch.countDown() }
        .build()

    countDownLatch.await(30, TimeUnit.SECONDS)

    sendRequest(engine)
    logEventLatch.await(30, TimeUnit.SECONDS)

    engine.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
    assertThat(logEventLatch.count).isEqualTo(0)
  }

  fun sendRequest(engine: Engine) {
    val client = engine.streamClient()

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = "example.com",
          path = "/test"
        )
        .build()

    client.newStreamPrototype().start().sendHeaders(requestHeaders, true)
  }
}

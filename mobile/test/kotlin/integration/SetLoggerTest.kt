package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Any
import com.google.protobuf.ByteString
import envoymobile.extensions.filters.http.test_logger.Filter.TestLogger
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SetLoggerTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `set logger`() {
    val countDownLatch = CountDownLatch(1)
    val logEventLatch = CountDownLatch(1)
    val configProto = TestLogger.newBuilder().getDefaultInstanceForType()
    var anyProto =
      Any.newBuilder()
        .setTypeUrl(
          "type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger"
        )
        .setValue(configProto.toByteString())
        .build()
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .addNativeFilter("test_logger", anyProto.toByteArray().toString(Charsets.UTF_8))
        .setLogger { _, msg ->
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
    var anyProto =
      Any.newBuilder()
        .setTypeUrl(
          "type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger"
        )
        .setValue(ByteString.empty())
        .build()

    val engine =
      EngineBuilder()
        .setEventTracker { event ->
          if (event["log_name"] == "event_name") {
            logEventLatch.countDown()
          }
        }
        .setLogLevel(LogLevel.DEBUG)
        .addNativeFilter("test_logger", anyProto.toByteArray().toString(Charsets.UTF_8))
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

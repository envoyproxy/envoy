package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class SetEventTrackerTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `set eventTracker`() {
    val countDownLatch = CountDownLatch(1)
    val engine = EngineBuilder()
      .setEventTracker { events ->
        for (entry in events) {
          assertThat(entry.key).isEqualTo("foo")
          assertThat(entry.value).isEqualTo("bar")
        }
        countDownLatch.countDown()
      }
      .addNativeFilter(
        "envoy.filters.http.test_event_tracker",
        "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker\",\"attributes\":{\"foo\":\"bar\"}}"
      )
      .build()

    val client = engine.streamClient()

    val requestHeaders = RequestHeadersBuilder(
      method = RequestMethod.GET,
      scheme = "https",
      authority = "example.com",
      path = "/test"
    )
      .build()

    client
      .newStreamPrototype()
      .start()
      .sendHeaders(requestHeaders, true)

    countDownLatch.await(30, TimeUnit.SECONDS)
    engine.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
  }

  @Test
  fun `engine should continue to run if no eventTracker is set`() {
    val countDownLatch = CountDownLatch(1)
    val client = EngineBuilder()
      .setOnEngineRunning {
        countDownLatch.countDown()
      }
      .build()

    countDownLatch.await(30, TimeUnit.SECONDS)
    client.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
  }
}

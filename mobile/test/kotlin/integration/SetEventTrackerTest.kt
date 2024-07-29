package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Any
import envoymobile.extensions.filters.http.test_event_tracker.Filter.TestEventTracker
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SetEventTrackerTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `set eventTracker`() {
    val countDownLatch = CountDownLatch(1)
    val configProto = TestEventTracker.newBuilder().putAttributes("foo", "bar").build()
    var anyProto =
      Any.newBuilder()
        .setTypeUrl(
          "type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker"
        )
        .setValue(configProto.toByteString())
        .build()
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setEventTracker { events ->
          for (entry in events) {
            assertThat(entry.key).isEqualTo("foo")
            assertThat(entry.value).isEqualTo("bar")
          }
          countDownLatch.countDown()
        }
        .addNativeFilter(
          "envoy.filters.http.test_event_tracker",
          anyProto.toByteArray().toString(Charsets.UTF_8)
        )
        .build()

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

    countDownLatch.await(30, TimeUnit.SECONDS)
    engine.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
  }

  @Test
  fun `engine should continue to run if no eventTracker is set and event is emitted`() {
    val countDownLatch = CountDownLatch(1)
    val configProto = TestEventTracker.newBuilder().putAttributes("foo", "bar").build()
    var anyProto =
      Any.newBuilder()
        .setTypeUrl(
          "type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker"
        )
        .setValue(configProto.toByteString())
        .build()
    val engine =
      EngineBuilder()
        .addNativeFilter(
          "envoy.filters.http.test_event_tracker",
          anyProto.toByteArray().toString(Charsets.UTF_8)
        )
        .build()

    val client = engine.streamClient()

    client
      .newStreamPrototype()
      .setOnResponseData { _, _, _ -> countDownLatch.countDown() }
      .start()
      .close(ByteBuffer.allocate(1))

    countDownLatch.await(30, TimeUnit.SECONDS)
    engine.terminate()
    assertThat(countDownLatch.count).isEqualTo(0)
  }
}

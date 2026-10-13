package test.kotlin.integration

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestFilter
import io.envoyproxy.envoymobile.RequestHeaders
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.RequestTrailers
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.StreamIntel
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

class ThrowingFilter : RequestFilter, ResponseFilter {
  override fun onRequestHeaders(
    headers: RequestHeaders,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterHeadersStatus<RequestHeaders> {
    throw Exception("Simulated onRequestHeaders exception")
  }

  override fun onRequestData(
    body: ByteBuffer,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterDataStatus<RequestHeaders> {
    return FilterDataStatus.Continue(body)
  }

  override fun onRequestTrailers(
    trailers: RequestTrailers,
    streamIntel: StreamIntel
  ): FilterTrailersStatus<RequestHeaders, RequestTrailers> {
    return FilterTrailersStatus.Continue(trailers)
  }

  override fun onResponseHeaders(
    headers: ResponseHeaders,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterHeadersStatus<ResponseHeaders> {
    throw Exception("Simulated onResponseHeaders exception")
  }

  override fun onResponseData(
    body: ByteBuffer,
    endStream: Boolean,
    streamIntel: StreamIntel
  ): FilterDataStatus<ResponseHeaders> {
    return FilterDataStatus.Continue(body)
  }

  override fun onResponseTrailers(
    trailers: ResponseTrailers,
    streamIntel: StreamIntel
  ): FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    return FilterTrailersStatus.Continue(trailers)
  }

  override fun onError(error: EnvoyError, finalStreamIntel: FinalStreamIntel) {}

  override fun onCancel(finalStreamIntel: FinalStreamIntel) {}

  override fun onComplete(finalStreamIntel: FinalStreamIntel) {}
}

@RunWith(RobolectricTestRunner::class)
class FilterThrowingExceptionTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpTestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP2_WITH_TLS)
  }

  @After
  fun tearDown() {
    httpTestServer.shutdown()
  }

  @Test
  fun `registers a filter that throws an exception and performs an HTTP request`() {
    val onEngineRunningLatch = CountDownLatch(1)
    val onResponseHeadersLatch = CountDownLatch(1)
    val onJNIExceptionEventLatch = CountDownLatch(2)

    var expectedMessages =
      mutableListOf(
        "Simulated onRequestHeaders exception||onRequestHeaders||",
        "Simulated onResponseHeaders exception||onResponseHeaders||"
      )

    val context = ApplicationProvider.getApplicationContext<Context>()
    val builder = AndroidEngineBuilder(context)
    val engine =
      builder
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .setEventTracker { event ->
          if (
            event["name"] == "event_log" && event["log_name"] == "jni_cleared_pending_exception"
          ) {
            assertThat(event["message"]).contains(expectedMessages.removeFirst())
            onJNIExceptionEventLatch.countDown()
          }
        }
        .addPlatformFilter(::ThrowingFilter)
        .setOnEngineRunning { onEngineRunningLatch.countDown() }
        .build()

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = httpTestServer.address,
          path = "/simple.txt"
        )
        .build()

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        println("here!!")
        val status = responseHeaders.httpStatus ?: 0L
        assertThat(status).isEqualTo(200)
        onResponseHeadersLatch.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    onResponseHeadersLatch.await(15, TimeUnit.SECONDS)
    assertThat(onResponseHeadersLatch.count).isEqualTo(0)

    onJNIExceptionEventLatch.await(15, TimeUnit.SECONDS)
    assertThat(onJNIExceptionEventLatch.count).isEqualTo(0)

    engine.terminate()
  }
}

package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
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

@RunWith(RobolectricTestRunner::class)
class CancelStreamTest {
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

  private val filterExpectation = CountDownLatch(1)
  private val runExpectation = CountDownLatch(1)

  class CancelValidationFilter(private val latch: CountDownLatch) : ResponseFilter {
    override fun onResponseHeaders(
      headers: ResponseHeaders,
      endStream: Boolean,
      streamIntel: StreamIntel
    ): FilterHeadersStatus<ResponseHeaders> {
      return FilterHeadersStatus.Continue(headers)
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

    override fun onComplete(finalStreamIntel: FinalStreamIntel) {}

    override fun onCancel(finalStreamIntel: FinalStreamIntel) {
      latch.countDown()
    }
  }

  @Test
  fun `cancel stream calls onCancel callback`() {
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .addPlatformFilter(
          name = "cancel_validation_filter",
          factory = { CancelValidationFilter(filterExpectation) }
        )
        .build()

    val client = engine.streamClient()

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = httpTestServer.address,
          path = "/simple.txt"
        )
        .build()

    client
      .newStreamPrototype()
      .setOnCancel { _ -> runExpectation.countDown() }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, false)
      .cancel()

    filterExpectation.await(10, TimeUnit.SECONDS)
    runExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(runExpectation.count).isEqualTo(0)
  }
}

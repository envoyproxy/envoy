package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ResetConnectivityStateTest {
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
  fun `successful request after connection drain`() {
    val headersExpectation = CountDownLatch(2)

    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
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

    var resultHeaders1: ResponseHeaders? = null
    var resultEndStream1: Boolean? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ ->
        resultHeaders1 = responseHeaders
        resultEndStream1 = endStream
        headersExpectation.countDown()
      }
      .setOnResponseData { _, endStream, _ -> resultEndStream1 = endStream }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)

    engine.resetConnectivityState()

    var resultHeaders2: ResponseHeaders? = null
    var resultEndStream2: Boolean? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ ->
        resultHeaders2 = responseHeaders
        resultEndStream2 = endStream
        headersExpectation.countDown()
      }
      .setOnResponseData { _, endStream, _ -> resultEndStream2 = endStream }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(headersExpectation.count).isEqualTo(0)
    assertThat(resultHeaders1!!.httpStatus).isEqualTo(200)
    assertThat(resultEndStream1).isTrue()
    assertThat(resultHeaders2!!.httpStatus).isEqualTo(200)
    assertThat(resultEndStream2).isTrue()
  }
}

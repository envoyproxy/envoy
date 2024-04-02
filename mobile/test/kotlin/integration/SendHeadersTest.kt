package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

class SendHeadersTest {
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
  fun `successful sending of request headers`() {
    val headersExpectation = CountDownLatch(1)

    val engine =
      EngineBuilder(Standard())
        .addLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .build()
    val client = engine.streamClient()

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = "localhost:${httpTestServer.port}",
          path = "/simple.txt"
        )
        .build()

    var resultHeaders: ResponseHeaders? = null
    var resultEndStream: Boolean? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ ->
        resultHeaders = responseHeaders
        resultEndStream = endStream
        headersExpectation.countDown()
      }
      .setOnResponseData { _, endStream, _ -> resultEndStream = endStream }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(headersExpectation.count).isEqualTo(0)

    assertThat(resultHeaders!!.httpStatus).isEqualTo(200)
    assertThat(resultEndStream).isTrue()
  }
}

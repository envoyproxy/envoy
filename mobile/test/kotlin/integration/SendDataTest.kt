package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

private const val ASSERTION_FILTER_TEXT_PROTO =
  """
  [type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion] {
  match_config {
    http_request_generic_body_match: {
      patterns: {
        string_match: 'request body'
      }
    }
  }
}
"""

class SendDataTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpTestServer =
      HttpTestServerFactory.start(
        HttpTestServerFactory.Type.HTTP2_WITH_TLS,
        mapOf(),
        "data",
        mapOf()
      )
  }

  @After
  fun tearDown() {
    httpTestServer.shutdown()
  }

  @Test
  fun `successful sending data`() {
    val expectation = CountDownLatch(1)
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
        .addNativeFilter("envoy.filters.http.assertion", ASSERTION_FILTER_TEXT_PROTO)
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

    var responseStatus: Int? = null
    var responseEndStream = false
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ -> responseStatus = headers.httpStatus }
      .setOnResponseData { _, endStream, _ ->
        // Sometimes Envoy Mobile may send an empty data (0 byte) with `endStream` set to true
        // to indicate an end of stream. So, we should only do `expectation.countDown()`
        // when the `endStream` is true.
        responseEndStream = endStream
        if (endStream) {
          expectation.countDown()
        }
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, false)
      .close(ByteBuffer.wrap("request body".toByteArray(Charsets.UTF_8)))

    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
    assertThat(responseEndStream).isTrue()
  }
}

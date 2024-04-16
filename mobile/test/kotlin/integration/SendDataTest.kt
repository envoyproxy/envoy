package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.Standard
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

private const val ASSERTION_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private const val REQUEST_STRING_MATCH = "match_me"

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
      EngineBuilder(Standard())
        .addLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
        .addNativeFilter(
          "envoy.filters.http.assertion",
          "[$ASSERTION_FILTER_TYPE] { match_config { http_request_generic_body_match: { patterns: { string_match: '$REQUEST_STRING_MATCH'}}}}"
        )
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

    val body = ByteBuffer.wrap(REQUEST_STRING_MATCH.toByteArray(Charsets.UTF_8))

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
      .close(body)

    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
    assertThat(responseEndStream).isTrue()
  }
}

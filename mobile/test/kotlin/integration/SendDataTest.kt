package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory.HttpTestServer
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
    AndroidJniLibrary.loadTestLibrary()
    JniLibrary.load()
  }

  private lateinit var httpTestServer: HttpTestServer

  @Before
  fun setUp() {
    httpTestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP2_WITH_TLS)
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
        .addNativeFilter(
          "envoy.filters.http.assertion",
          "{'@type': $ASSERTION_FILTER_TYPE, match_config: {http_request_generic_body_match: {patterns: [{string_match: $REQUEST_STRING_MATCH}]}}}"
        )
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
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
      .setOnResponseHeaders { headers, endStream, _ ->
        responseStatus = headers.httpStatus
        responseEndStream = endStream
        expectation.countDown()
      }
      .setOnResponseData { _, endStream, _ -> responseEndStream = endStream }
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

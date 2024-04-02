package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.fail
import org.junit.Test

private const val ASSERTION_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private const val TEST_RESPONSE_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse"
private const val REQUEST_STRING_MATCH = "match_me"

// TODO(fredyw): Figure out why HttpTestServer prevents EngineBuilder from using native filters.
class SendDataTest {
  init {
    JniLibrary.loadTestLibrary()
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
        .addNativeFilter("test_remote_response", "{'@type': $TEST_RESPONSE_FILTER_TYPE}")
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
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

package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.RequestTrailersBuilder
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val TEST_RESPONSE_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse"
private const val ASSERTION_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private const val MATCHER_TRAILER_NAME = "test-trailer"
private const val MATCHER_TRAILER_VALUE = "test.code"

class SendTrailersTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful sending of trailers`() {

    val expectation = CountDownLatch(1)
    val engine =
      EngineBuilder(Standard())
        .addNativeFilter(
          "envoy.filters.http.assertion",
          "{'@type': $ASSERTION_FILTER_TYPE, match_config: {http_request_trailers_match: {headers: [{name: 'test-trailer', exact_match: 'test.code'}]}}}"
        )
        .addNativeFilter(
          "envoy.filters.http.buffer",
          "{\"@type\":\"type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer\",\"max_request_bytes\":65000}"
        )
        .addNativeFilter("test_remote_response", "{'@type': $TEST_RESPONSE_FILTER_TYPE}")
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

    val body = ByteBuffer.wrap("match_me".toByteArray(Charsets.UTF_8))
    val requestTrailers =
      RequestTrailersBuilder().add(MATCHER_TRAILER_NAME, MATCHER_TRAILER_VALUE).build()

    var responseStatus: Int? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, false)
      .sendData(body)
      .close(requestTrailers)

    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
  }
}

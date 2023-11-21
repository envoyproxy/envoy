package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val TEST_RESPONSE_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse"

// This test doesn't do what it advertises (https://github.com/envoyproxy/envoy/issues/25180)
class ResetConnectivityStateTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful request after connection drain`() {
    val headersExpectation = CountDownLatch(2)

    val engine =
      EngineBuilder(Standard())
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

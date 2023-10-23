package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
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

class ReceiveDataTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `response headers and response data call onResponseHeaders and onResponseData`() {

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

    val headersExpectation = CountDownLatch(1)
    val dataExpectation = CountDownLatch(1)

    var status: Int? = null
    var body: ByteBuffer? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        status = responseHeaders.httpStatus
        headersExpectation.countDown()
      }
      .setOnResponseData { data, _, _ ->
        body = data
        dataExpectation.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)
    dataExpectation.await(10, TimeUnit.SECONDS)
    engine.terminate()

    assertThat(headersExpectation.count).isEqualTo(0)
    assertThat(dataExpectation.count).isEqualTo(0)

    assertThat(status).isEqualTo(200)
    assertThat(body!!.array().toString(Charsets.UTF_8)).isEqualTo("data")
  }
}

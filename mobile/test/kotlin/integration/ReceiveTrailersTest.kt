package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.RequestTrailersBuilder
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.StreamIntel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val TEST_RESPONSE_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse"
private const val MATCHER_TRAILER_NAME = "test-trailer"
private const val MATCHER_TRAILER_VALUE = "test.code"

class ReceiveTrailersTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  class ErrorValidationFilter(private val onTrailers: CountDownLatch) : ResponseFilter {
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
      onTrailers.countDown()
      return FilterTrailersStatus.Continue(trailers)
    }

    override fun onError(error: EnvoyError, finalStreamIntel: FinalStreamIntel) {}

    override fun onComplete(finalStreamIntel: FinalStreamIntel) {}

    override fun onCancel(finalStreamIntel: FinalStreamIntel) {}
  }

  @Test
  fun `successful sending of trailers`() {
    val trailersReceived = CountDownLatch(2)
    val expectation = CountDownLatch(2)
    val engine =
      EngineBuilder(Standard())
        .addPlatformFilter(
          name = "test_platform_filter",
          factory = { ErrorValidationFilter(trailersReceived) }
        )
        .addNativeFilter("test_remote_response", "{'@type': $TEST_RESPONSE_FILTER_TYPE}")
        .build()

    val client = engine.streamClient()

    val builder =
      RequestHeadersBuilder(
        method = RequestMethod.GET,
        scheme = "https",
        authority = "example.com",
        path = "/test"
      )
    builder.add("send-trailers", "true")
    val requestHeadersDefault = builder.build()

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
      .sendHeaders(requestHeadersDefault, false)
      .sendData(body)
      .close(requestTrailers)

    expectation.await(10, TimeUnit.SECONDS)

    builder.remove("send-trailers")
    builder.add("send-trailers", "empty")
    val requestHeadersEmpty = builder.build()
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeadersEmpty, false)
      .sendData(body)
      .close(requestTrailers)
    expectation.await(10, TimeUnit.SECONDS)

    builder.remove("send-trailers")
    builder.add("send-trailers", "empty-value")
    val requestHeadersEmptyValue = builder.build()
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeadersEmptyValue, false)
      .sendData(body)
      .close(requestTrailers)
    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(trailersReceived.count).isEqualTo(0)
    trailersReceived.await(10, TimeUnit.SECONDS)
    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
  }
}

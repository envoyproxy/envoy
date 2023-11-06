package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.Standard
import io.envoyproxy.envoymobile.StreamIntel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val TEST_RESPONSE_FILTER_TYPE =
  "type.googleapis.com/" +
    "envoymobile.extensions.filters.http.test_remote_response.TestRemoteResponse"

class StreamIdleTimeoutTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  private val filterExpectation = CountDownLatch(1)
  private val callbackExpectation = CountDownLatch(1)

  class IdleTimeoutValidationFilter(private val latch: CountDownLatch) : ResponseFilter {
    override fun onResponseHeaders(
      headers: ResponseHeaders,
      endStream: Boolean,
      streamIntel: StreamIntel
    ): FilterHeadersStatus<ResponseHeaders> {
      return FilterHeadersStatus.StopIteration()
    }

    override fun onResponseData(
      body: ByteBuffer,
      endStream: Boolean,
      streamIntel: StreamIntel
    ): FilterDataStatus<ResponseHeaders> {
      return FilterDataStatus.StopIterationNoBuffer()
    }

    override fun onResponseTrailers(
      trailers: ResponseTrailers,
      streamIntel: StreamIntel
    ): FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
      return FilterTrailersStatus.StopIteration()
    }

    override fun onError(error: EnvoyError, finalStreamIntel: FinalStreamIntel) {
      assertThat(error.errorCode).isEqualTo(4)
      latch.countDown()
    }

    override fun onComplete(finalStreamIntel: FinalStreamIntel) {}

    override fun onCancel(finalStreamIntel: FinalStreamIntel) {
      fail<StreamIdleTimeoutTest>("Unexpected call to onCancel filter callback")
    }
  }

  @Test
  fun `stream idle timeout triggers onError callbacks`() {
    val engine =
      EngineBuilder(Standard())
        .addPlatformFilter(
          name = "idle_timeout_validation_filter",
          factory = { IdleTimeoutValidationFilter(filterExpectation) }
        )
        .addNativeFilter("test_remote_response", "{'@type': $TEST_RESPONSE_FILTER_TYPE}")
        .addStreamIdleTimeoutSeconds(1)
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

    client
      .newStreamPrototype()
      .setOnError { error, _ ->
        assertThat(error.errorCode).isEqualTo(4)
        callbackExpectation.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, false)

    filterExpectation.await(10, TimeUnit.SECONDS)
    callbackExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(callbackExpectation.count).isEqualTo(0)
  }
}

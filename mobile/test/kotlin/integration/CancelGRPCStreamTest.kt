package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.GRPCClient
import io.envoyproxy.envoymobile.GRPCRequestHeadersBuilder
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
import org.junit.Test

private const val FILTER_NAME = "cancel_validation_filter"
private const val PBF_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
private const val LOCAL_ERROR_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"

class CancelGRPCStreamTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  private val filterExpectation = CountDownLatch(1)
  private val onCancelCallbackExpectation = CountDownLatch(1)

  class CancelValidationFilter(private val latch: CountDownLatch) : ResponseFilter {
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
      return FilterTrailersStatus.Continue(trailers)
    }

    override fun onError(error: EnvoyError, finalStreamIntel: FinalStreamIntel) {}

    override fun onComplete(finalStreamIntel: FinalStreamIntel) {}

    override fun onCancel(finalStreamIntel: FinalStreamIntel) {
      latch.countDown()
    }
  }

  @Test
  fun `cancel grpc stream calls onCancel callback`() {
    val engine =
      EngineBuilder(Standard())
        .addPlatformFilter(
          name = FILTER_NAME,
          factory = { CancelValidationFilter(filterExpectation) }
        )
        .addNativeFilter(
          "envoy.filters.http.platform_bridge",
          "{'@type': $PBF_TYPE, platform_filter_name: $FILTER_NAME}"
        )
        .addNativeFilter("envoy.filters.http.local_error", "{'@type': $LOCAL_ERROR_FILTER_TYPE}")
        .build()

    val client = GRPCClient(engine.streamClient())

    val requestHeaders =
      GRPCRequestHeadersBuilder(scheme = "https", authority = "example.com", path = "/test").build()

    client
      .newGRPCStreamPrototype()
      .setOnCancel { _ -> onCancelCallbackExpectation.countDown() }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, false)
      .cancel()

    filterExpectation.await(10, TimeUnit.SECONDS)
    onCancelCallbackExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(onCancelCallbackExpectation.count).isEqualTo(0)
  }
}

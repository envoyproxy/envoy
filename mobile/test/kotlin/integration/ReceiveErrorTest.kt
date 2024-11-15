package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.common.truth.Truth.assertWithMessage
import com.google.protobuf.Any
import com.google.protobuf.ByteString
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.FinalStreamIntel
import io.envoyproxy.envoymobile.GRPCRequestHeadersBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.StreamIntel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

private const val LOCAL_ERROR_FILTER_TYPE =
  "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
private const val FILTER_NAME = "error_validation_filter"

@RunWith(RobolectricTestRunner::class)
class ReceiveErrorTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private val callbackReceivedError = CountDownLatch(1)
  private val filterReceivedError = CountDownLatch(1)
  private val filterNotCancelled = CountDownLatch(1)

  class ErrorValidationFilter(
    private val receivedError: CountDownLatch,
    private val notCancelled: CountDownLatch
  ) : ResponseFilter {
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

    override fun onError(error: EnvoyError, finalStreamIntel: FinalStreamIntel) {
      receivedError.countDown()
    }

    override fun onComplete(finalStreamIntel: FinalStreamIntel) {}

    override fun onCancel(finalStreamIntel: FinalStreamIntel) {
      notCancelled.countDown()
    }
  }

  @Test
  fun `errors on stream call onError callback`() {
    val requestHeader =
      GRPCRequestHeadersBuilder(
          scheme = "https",
          authority = "doesnotexist.example.com",
          path = "/test"
        )
        .build()

    var anyProto =
      Any.newBuilder().setTypeUrl(LOCAL_ERROR_FILTER_TYPE).setValue(ByteString.empty()).build()

    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .addPlatformFilter(
          name = FILTER_NAME,
          factory = { ErrorValidationFilter(filterReceivedError, filterNotCancelled) }
        )
        .addNativeFilter(
          "envoy.filters.http.local_error",
          anyProto.toByteArray().toString(Charsets.UTF_8)
        )
        .build()

    var errorCode: Int? = null

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { _, _, _ -> fail("Headers received instead of expected error") }
      .setOnResponseData { _, _, _ -> fail("Data received instead of expected error") }
      // The unmatched expectation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { error, _ ->
        errorCode = error.errorCode
        callbackReceivedError.countDown()
      }
      .setOnCancel { _ -> fail("Unexpected call to onCancel response callback") }
      .start()
      .sendHeaders(requestHeader, true)

    filterReceivedError.await(10, TimeUnit.SECONDS)
    filterNotCancelled.await(1, TimeUnit.SECONDS)
    callbackReceivedError.await(10, TimeUnit.SECONDS)
    engine.terminate()

    assertWithMessage("Missing call to onError filter callback")
      .that(filterReceivedError.count)
      .isEqualTo(0)

    assertWithMessage("Unexpected call to onCancel filter callback")
      .that(filterNotCancelled.count)
      .isEqualTo(1)

    assertWithMessage("Missing call to onError response callback")
      .that(callbackReceivedError.count)
      .isEqualTo(0)

    assertThat(errorCode).isEqualTo(2) // 503/Connection Failure
  }
}

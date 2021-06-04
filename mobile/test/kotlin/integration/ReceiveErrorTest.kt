package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.GRPCRequestHeadersBuilder
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val hcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
private const val pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
private const val localErrorFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
private const val filterName = "error_validation_filter"
private const val config =
"""
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": $hcmType
        stat_prefix: hcm
        route_config:
          name: api_router
          virtual_hosts:
          - name: api
            domains: ["*"]
            routes:
            - match: { prefix: "/" }
              direct_response: { status: 503 }
        http_filters:
        - name: envoy.filters.http.platform_bridge
          typed_config:
            "@type": $pbfType
            platform_filter_name: $filterName
        - name: envoy.filters.http.local_error
          typed_config:
            "@type": $localErrorFilterType
        - name: envoy.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

class ReceiveErrorTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private val filterExpectation = CountDownLatch(1)
  private val runExpectation = CountDownLatch(1)

  class ErrorValidationFilter(
    private val latch: CountDownLatch
  ) : ResponseFilter {
    override fun onResponseHeaders(headers: ResponseHeaders, endStream: Boolean): FilterHeadersStatus<ResponseHeaders> {
      return FilterHeadersStatus.Continue(headers)
    }

    override fun onResponseData(body: ByteBuffer, endStream: Boolean): FilterDataStatus<ResponseHeaders> {
      return FilterDataStatus.Continue(body)
    }

    override fun onResponseTrailers(trailers: ResponseTrailers): FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
      return FilterTrailersStatus.Continue(trailers)
    }

    override fun onError(error: EnvoyError) {}

    override fun onCancel() {
      latch.countDown()
    }
  }

  @Test
  fun `errors on stream call onError callback`() {
    val requestHeader = GRPCRequestHeadersBuilder(
      scheme = "https",
      authority = "example.com",
      path = "/pb.api.v1.Foo/GetBar"
    ).build()

    val engine = EngineBuilder(Custom(config))
      .addPlatformFilter(
        name = filterName,
        factory = { ErrorValidationFilter(filterExpectation) }
      )
      .setOnEngineRunning {}
      .build()

    var errorCode: Int? = null

    engine.streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { _, _ -> fail("Headers received instead of expected error") }
      .setOnResponseData { _, _ -> fail("Data received instead of expected error") }
      // The unmatched expectation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { error ->
        errorCode = error.errorCode
        runExpectation.countDown()
      }
      .start()
      .sendHeaders(requestHeader, true)

    engine.terminate()

    filterExpectation.await(10, TimeUnit.SECONDS)
    runExpectation.await(10, TimeUnit.SECONDS)

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(runExpectation.count).isEqualTo(0)

    assertThat(errorCode).isEqualTo(2) // 503/Connection Failure
  }
}

package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.EnvoyError
import io.envoyproxy.envoymobile.FilterDataStatus
import io.envoyproxy.envoymobile.FilterHeadersStatus
import io.envoyproxy.envoymobile.FilterTrailersStatus
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseFilter
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.ResponseTrailers
import io.envoyproxy.envoymobile.StreamIntel
import io.envoyproxy.envoymobile.UpstreamHttpProtocol
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val idleTimeout = "0.5s"
private const val ehcmType =
  "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
private const val lefType =
  "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
private const val pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
private const val filterName = "idle_timeout_validation_filter"
private const val config =

"""
static_resources:
  listeners:
  - name: fake_remote_listener
    address:
      socket_address: { protocol: TCP, address: 127.0.0.1, port_value: 10101 }
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": $ehcmType
          stat_prefix: remote_hcm
          route_config:
            name: remote_route
            virtual_hosts:
            - name: remote_service
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                direct_response: { status: 200 }
          http_filters:
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": $hcmType
        stat_prefix: api_hcm
        stream_idle_timeout: $idleTimeout
        route_config:
          name: api_router
          virtual_hosts:
          - name: api
            domains: ["*"]
            routes:
            - match: { prefix: "/" }
              route: { cluster: fake_remote }
        http_filters:
        - name: envoy.filters.http.local_error
          typed_config:
            "@type": $lefType
        - name: envoy.filters.http.platform_bridge
          typed_config:
            "@type": $pbfType
            platform_filter_name: $filterName
        - name: envoy.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: fake_remote
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_remote
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 127.0.0.1, port_value: 10101 }
"""

class CancelStreamTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  private val filterExpectation = CountDownLatch(1)
  private val callbackExpectation = CountDownLatch(1)

  class IdleTimeoutValidationFilter(
    private val latch: CountDownLatch
  ) : ResponseFilter {
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

    override fun onError(error: EnvoyError, streamIntel: StreamIntel) {
      assertThat(error.errorCode).isEqualTo(4)
      latch.countDown()
    }

    override fun onCancel(streamIntel: StreamIntel) {
      fail<CancelStreamTest>("Unexpected call to onCancel filter callback")
    }
  }

  @Test
  fun `stream idle timeout triggers onError callbacks`() {
    val engine = EngineBuilder(Custom(config))
      .addPlatformFilter(
        name = "idle_timeout_validation_filter",
        factory = { IdleTimeoutValidationFilter(filterExpectation) }
      )
      .setOnEngineRunning {}
      .build()

    val client = engine.streamClient()

    val requestHeaders = RequestHeadersBuilder(
      method = RequestMethod.GET,
      scheme = "https",
      authority = "example.com",
      path = "/test"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
      .build()

    client.newStreamPrototype()
      .setOnError { error, _ ->
        assertThat(error.errorCode).isEqualTo(4)
        callbackExpectation.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    filterExpectation.await(10, TimeUnit.SECONDS)
    callbackExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(callbackExpectation.count).isEqualTo(0)
  }
}

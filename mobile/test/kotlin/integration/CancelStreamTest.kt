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
import io.envoyproxy.envoymobile.UpstreamHttpProtocol
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

private const val hcmType =
  "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
private const val pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
private const val filterName = "cancel_validation_filter"
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
              "@type": $hcmType
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
            route_config:
              name: api_router
              virtual_hosts:
              - name: api
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: fake_remote }
            http_filters:
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
  private val runExpectation = CountDownLatch(1)

  class CancelValidationFilter(
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
  fun `cancel stream calls onCancel callback`() {
    val engine = EngineBuilder(Custom(config))
      .addPlatformFilter(
        name = "cancel_validation_filter",
        factory = { CancelValidationFilter(filterExpectation) }
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
      .setOnCancel {
        runExpectation.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, false)
      .cancel()

    filterExpectation.await(10, TimeUnit.SECONDS)
    runExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(filterExpectation.count).isEqualTo(0)
    assertThat(runExpectation.count).isEqualTo(0)
  }
}

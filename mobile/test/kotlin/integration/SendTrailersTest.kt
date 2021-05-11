package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.RequestTrailersBuilder
import io.envoyproxy.envoymobile.UpstreamHttpProtocol
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
private const val assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private const val matcherTrailerName = "test-trailer"
private const val matcherTrailerValue = "test.code"
private const val config =
  """
    static_resources:
      listeners:
      - name: base_api_listener
        address:
          socket_address:
            protocol: TCP
            address: 0.0.0.0
            port_value: 10000
        api_listener:
          api_listener:
            "@type": $apiListenerType
            stat_prefix: hcm
            route_config:
              name: api_router
              virtual_hosts:
                - name: api
                  domains:
                    - "*"
                  routes:
                    - match:
                        prefix: "/"
                      direct_response:
                        status: 200
            http_filters:
              - name: envoy.filters.http.assertion
                typed_config:
                  "@type": $assertionFilterType
                  match_config:
                    http_request_trailers_match:
                      headers:
                        - name: $matcherTrailerName
                          exact_match: $matcherTrailerValue
              - name: envoy.filters.http.buffer
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
                  max_request_bytes: 65000
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    """
class SendTrailersTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful sending of trailers`() {

    val expectation = CountDownLatch(1)
    val engine = EngineBuilder(Custom(config))
      .setOnEngineRunning { }
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

    val body = ByteBuffer.wrap("match_me".toByteArray(Charsets.UTF_8))
    val requestTrailers = RequestTrailersBuilder()
      .add(matcherTrailerName, matcherTrailerValue)
      .build()

    var responseStatus: Int? = null
    client.newStreamPrototype()
      .setOnResponseHeaders { headers, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnError {
        fail("Unexpected error")
      }
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

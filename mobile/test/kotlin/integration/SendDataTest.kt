package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.UpstreamHttpProtocol
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private const val apiListenerType =
  "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
private const val assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private const val requestStringMatch = "match_me"
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
        config:
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
                  http_request_generic_body_match:
                    patterns:
                      - string_match: $requestStringMatch
            - name: envoy.filters.http.buffer
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
                max_request_bytes: 65000
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

class SendDataTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful sending data`() {
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

    val body = ByteBuffer.wrap(requestStringMatch.toByteArray(Charsets.UTF_8))

    var responseStatus: Int? = null
    var responseHeadersEndStream = false
    client.newStreamPrototype()
      .setOnResponseHeaders { headers, endStream, _ ->
        responseStatus = headers.httpStatus
        responseHeadersEndStream = endStream
        expectation.countDown()
      }
      .setOnError { _, _, _ ->
        fail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, false)
      .close(body)

    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
    assertThat(responseHeadersEndStream).isTrue()
  }
}

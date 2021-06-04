package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.ResponseHeaders
import io.envoyproxy.envoymobile.UpstreamHttpProtocol
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.assertj.core.api.Assertions.fail
import org.junit.Test

private val apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
private val assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
private val config =
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
                http_request_headers_match:
                  headers:
                    - name: ":authority"
                      exact_match: example.com
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

class SendHeadersTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful sending of request headers`() {
    val headersExpectation = CountDownLatch(1)

    val engine = EngineBuilder(Custom(config)).build()
    val client = engine.streamClient()

    val requestHeaders = RequestHeadersBuilder(
      method = RequestMethod.GET,
      scheme = "https",
      authority = "example.com",
      path = "/test"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
      .build()

    var resultHeaders: ResponseHeaders? = null
    var resultEndStream: Boolean? = null
    client.newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream ->
        resultHeaders = responseHeaders
        resultEndStream = endStream
        headersExpectation.countDown()
      }
      .setOnError { fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(headersExpectation.count).isEqualTo(0)

    assertThat(resultHeaders!!.httpStatus).isEqualTo(200)
    assertThat(resultEndStream).isTrue()
  }
}

package test.kotlin.integration

import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.KeyValueStore
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
private const val testKey = "foo"
private const val testValue = "bar"
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
            - name: envoy.filters.http.test_kv_store
              typed_config:
                "@type": type.googleapis.com/envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore
                kv_store_name: envoy.key_value.platform_test
                test_key: $testKey
                test_value: $testValue
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

class KeyValueStoreTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `a registered KeyValueStore implementation handles calls from a TestKeyValueStore filter`() {

    val readExpectation = CountDownLatch(3)
    val saveExpectation = CountDownLatch(1)
    val testKeyValueStore = KeyValueStore(
      read = { _ -> readExpectation.countDown(); null },
      remove = { _ -> {}},
      save = { _, _ -> saveExpectation.countDown() }
    )

    val engine = EngineBuilder(Custom(config))
        .addKeyValueStore("envoy.key_value.platform_test", testKeyValueStore)
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
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    readExpectation.await(10, TimeUnit.SECONDS)
    saveExpectation.await(10, TimeUnit.SECONDS)
    engine.terminate()

    assertThat(readExpectation.count).isEqualTo(0)
    assertThat(saveExpectation.count).isEqualTo(0)
  }
}

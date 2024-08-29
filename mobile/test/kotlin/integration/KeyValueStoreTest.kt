package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Any
import envoymobile.extensions.filters.http.test_kv_store.Filter.TestKeyValueStore
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.KeyValueStore
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

private const val TEST_KEY = "foo"
private const val TEST_VALUE = "bar"

@RunWith(RobolectricTestRunner::class)
class KeyValueStoreTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpTestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP2_WITH_TLS)
  }

  @After
  fun tearDown() {
    httpTestServer.shutdown()
  }

  @Test
  fun `a registered KeyValueStore implementation handles calls from a TestKeyValueStore filter`() {
    val readExpectation = CountDownLatch(3)
    val saveExpectation = CountDownLatch(1)
    val testKeyValueStore =
      object : KeyValueStore {
        override fun read(key: String): String? {
          readExpectation.countDown()
          return null
        }

        override fun remove(key: String) {}

        override fun save(key: String, value: String) {
          saveExpectation.countDown()
        }
      }

    val configProto =
      TestKeyValueStore.newBuilder()
        .setKvStoreName("envoy.key_value.platform_test")
        .setTestKey(TEST_KEY)
        .setTestValue(TEST_VALUE)
        .build()
    var anyProto =
      Any.newBuilder()
        .setTypeUrl(
          "type.googleapis.com/envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore"
        )
        .setValue(configProto.toByteString())
        .build()

    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .addKeyValueStore("envoy.key_value.platform_test", testKeyValueStore)
        .addNativeFilter(
          "envoy.filters.http.test_kv_store",
          anyProto.toByteArray().toString(Charsets.UTF_8)
        )
        .build()
    val client = engine.streamClient()

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = httpTestServer.address,
          path = "/simple.txt"
        )
        .build()

    client
      .newStreamPrototype()
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

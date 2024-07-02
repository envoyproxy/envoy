package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SendDataTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpTestServer =
      HttpTestServerFactory.start(
        HttpTestServerFactory.Type.HTTP2_WITH_TLS,
        mapOf(),
        "data",
        mapOf()
      )
  }

  @After
  fun tearDown() {
    httpTestServer.shutdown()
  }

  @Test
  fun `successful sending data`() {
    val expectation = CountDownLatch(1)

    val string_match =
      io.envoyproxy.envoy.config.common.matcher.v3.HttpGenericBodyMatch.GenericTextMatch
        .newBuilder()
        .setStringMatch("request body")
        .build()
    val match =
      io.envoyproxy.envoy.config.common.matcher.v3.HttpGenericBodyMatch.newBuilder()
        .addPatterns(string_match)
        .build()
    val match_config =
      io.envoyproxy.envoy.config.common.matcher.v3.MatchPredicate.newBuilder()
        .setHttpRequestGenericBodyMatch(match)
        .build()
    val config_proto =
      envoymobile.extensions.filters.http.assertion.Filter.Assertion.newBuilder()
        .setMatchConfig(match_config)
        .build()
    var any_proto =
      com.google.protobuf.Any.newBuilder()
        .setTypeUrl("type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion")
        .setValue(config_proto.toByteString())
        .build()

    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
        .addNativeFilter("envoy.filters.http.assertion", String(any_proto.toByteArray()))
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

    var responseStatus: Int? = null
    var responseEndStream = false
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ -> responseStatus = headers.httpStatus }
      .setOnResponseData { _, endStream, _ ->
        // Sometimes Envoy Mobile may send an empty data (0 byte) with `endStream` set to true
        // to indicate an end of stream. So, we should only do `expectation.countDown()`
        // when the `endStream` is true.
        responseEndStream = endStream
        if (endStream) {
          expectation.countDown()
        }
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, false)
      .close(ByteBuffer.wrap("request body".toByteArray(Charsets.UTF_8)))

    expectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
    assertThat(responseEndStream).isTrue()
  }
}

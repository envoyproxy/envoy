package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.RequestTrailersBuilder
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
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

private const val MATCHER_TRAILER_NAME = "test-trailer"
private const val MATCHER_TRAILER_VALUE = "test.code"

@RunWith(RobolectricTestRunner::class)
class SendTrailersTest {
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
  fun `successful sending of trailers`() {
    val expectation = CountDownLatch(1)
    val match = io.envoyproxy.envoy.type.matcher.v3.StringMatcher.newBuilder().setExact("test.code")
    val trailers =
      io.envoyproxy.envoy.config.route.v3.HeaderMatcher.newBuilder()
        .setName("test-trailer")
        .setStringMatch(match)
        .build()
    val headers_match =
      io.envoyproxy.envoy.config.common.matcher.v3.HttpHeadersMatch.newBuilder()
        .addHeaders(trailers)
        .build()
    val match_config =
      io.envoyproxy.envoy.config.common.matcher.v3.MatchPredicate.newBuilder()
        .setHttpRequestTrailersMatch(headers_match)
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
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
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

    val body = ByteBuffer.wrap("match_me".toByteArray(Charsets.UTF_8))
    val requestTrailers =
      RequestTrailersBuilder().add(MATCHER_TRAILER_NAME, MATCHER_TRAILER_VALUE).build()

    var responseStatus: Int? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
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

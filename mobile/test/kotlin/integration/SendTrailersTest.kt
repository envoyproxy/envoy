package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Any
import envoymobile.extensions.filters.http.assertion.Filter.Assertion
import io.envoyproxy.envoy.config.common.matcher.v3.HttpHeadersMatch
import io.envoyproxy.envoy.config.common.matcher.v3.MatchPredicate
import io.envoyproxy.envoy.config.route.v3.HeaderMatcher
import io.envoyproxy.envoy.type.matcher.v3.StringMatcher
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
    StringMatcher.newBuilder().setExact("test.code")
    val match = StringMatcher.newBuilder().setExact("test.code")
    val trailers = HeaderMatcher.newBuilder().setName("test-trailer").setStringMatch(match).build()
    val headersMatch = HttpHeadersMatch.newBuilder().addHeaders(trailers).build()
    val matchConfig = MatchPredicate.newBuilder().setHttpRequestTrailersMatch(headersMatch).build()
    val configProto = Assertion.newBuilder().setMatchConfig(matchConfig).build()
    var anyProto =
      Any.newBuilder()
        .setTypeUrl("type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion")
        .setValue(configProto.toByteString())
        .build()

    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .addNativeFilter(
          "envoy.filters.http.assertion",
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

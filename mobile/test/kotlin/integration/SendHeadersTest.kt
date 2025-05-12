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
import io.envoyproxy.envoymobile.ResponseHeaders
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

@RunWith(RobolectricTestRunner::class)
class SendHeadersTest {
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
  fun `successful sending of request headers`() {
    val headersExpectation = CountDownLatch(1)

    val match1 =
      HeaderMatcher.newBuilder()
        .setName(":method")
        .setStringMatch(StringMatcher.newBuilder().setExact("GET"))
        .build()
    val match2 =
      HeaderMatcher.newBuilder()
        .setName(":scheme")
        .setStringMatch(StringMatcher.newBuilder().setExact("https"))
        .build()
    val match3 =
      HeaderMatcher.newBuilder()
        .setName(":path")
        .setStringMatch(StringMatcher.newBuilder().setExact("/simple.txt"))
        .build()
    val headersMatch =
      HttpHeadersMatch.newBuilder().addHeaders(match1).addHeaders(match2).addHeaders(match3)
    val matchConfig = MatchPredicate.newBuilder().setHttpRequestHeadersMatch(headersMatch).build()
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

    var resultHeaders: ResponseHeaders? = null
    var resultEndStream: Boolean? = null
    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ ->
        resultHeaders = responseHeaders
        resultEndStream = endStream
        headersExpectation.countDown()
      }
      .setOnResponseData { _, endStream, _ -> resultEndStream = endStream }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeaders, true)

    headersExpectation.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(headersExpectation.count).isEqualTo(0)

    assertThat(resultHeaders!!.httpStatus).isEqualTo(200)
    assertThat(resultEndStream).isTrue()
  }
}

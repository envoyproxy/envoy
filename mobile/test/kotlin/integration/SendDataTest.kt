package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Any
import envoymobile.extensions.filters.http.assertion.Filter.Assertion
import io.envoyproxy.envoy.config.common.matcher.v3.HttpGenericBodyMatch
import io.envoyproxy.envoy.config.common.matcher.v3.HttpGenericBodyMatch.GenericTextMatch
import io.envoyproxy.envoy.config.common.matcher.v3.MatchPredicate
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
        0,
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

    val stringMatch = GenericTextMatch.newBuilder().setStringMatch("request body").build()
    val match = HttpGenericBodyMatch.newBuilder().addPatterns(stringMatch).build()
    val matchConfig = MatchPredicate.newBuilder().setHttpRequestGenericBodyMatch(match).build()
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
        .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
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

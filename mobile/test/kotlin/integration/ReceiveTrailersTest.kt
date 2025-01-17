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

private const val TRAILER_NAME = "test-trailer"
private const val TRAILER_VALUE = "test.code"

@RunWith(RobolectricTestRunner::class)
class ReceiveTrailersTest {
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
        mapOf(TRAILER_NAME to TRAILER_VALUE)
      )
  }

  @After
  fun tearDown() {
    httpTestServer.shutdown()
  }

  @Test
  fun `successful sending of trailers`() {
    val trailersReceived = CountDownLatch(1)
    val expectation = CountDownLatch(1)
    val engine =
      EngineBuilder()
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .build()

    val client = engine.streamClient()

    val builder =
      RequestHeadersBuilder(
        method = RequestMethod.GET,
        scheme = "https",
        authority = httpTestServer.address,
        path = "/simple.txt"
      )
    val requestHeadersDefault = builder.build()

    val body = ByteBuffer.wrap("match_me".toByteArray(Charsets.UTF_8))
    val requestTrailers = RequestTrailersBuilder().add(TRAILER_NAME, TRAILER_VALUE).build()

    var responseStatus: Int? = null
    val trailerValues = mutableListOf<String>()
    client
      .newStreamPrototype()
      .setOnResponseHeaders { headers, _, _ ->
        responseStatus = headers.httpStatus
        expectation.countDown()
      }
      .setOnResponseTrailers { trailers, _ ->
        val values = trailers.value(TRAILER_NAME)
        if (values != null) {
          trailerValues += values
        }
        trailersReceived.countDown()
      }
      .setOnError { _, _ -> fail("Unexpected error") }
      .start()
      .sendHeaders(requestHeadersDefault, false)
      .sendData(body)
      .close(requestTrailers)

    expectation.await(10, TimeUnit.SECONDS)
    trailersReceived.await(10, TimeUnit.SECONDS)

    engine.terminate()

    assertThat(trailersReceived.count).isEqualTo(0)
    assertThat(expectation.count).isEqualTo(0)
    assertThat(responseStatus).isEqualTo(200)
    assertThat(trailerValues).contains(TRAILER_VALUE)
  }
}

package test.kotlin.integration

import io.envoyproxy.envoymobile.Standard
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

class SendHeadersTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `successful sending of request headers`() {
    val headersExpectation = CountDownLatch(1)

    val engine = EngineBuilder(Standard()).build()
    val client = engine.streamClient()

    val requestHeaders = RequestHeadersBuilder(
      method = RequestMethod.GET,
      scheme = "https",
      authority = "api.lyft.com",
      path = "/ping"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
      .build()

    var resultHeaders: ResponseHeaders? = null
    var resultEndStream: Boolean? = null
    client.newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ ->
        resultHeaders = responseHeaders
        resultEndStream = endStream
        headersExpectation.countDown()
      }
      .setOnResponseData { _, endStream, _ ->
        resultEndStream = endStream
      }
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

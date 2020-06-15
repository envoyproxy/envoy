package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import java.nio.ByteBuffer
import java.util.concurrent.Executor
import org.junit.Test
import org.mockito.ArgumentMatchers.any
import org.mockito.Mockito.`when`
import org.mockito.Mockito.mock
import org.mockito.Mockito.verify

class EnvoyClientTest {
  private val engine = mock(EnvoyEngine::class.java)
  private val stream = mock(EnvoyHTTPStream::class.java)
  private val config = EnvoyConfiguration(
    "stats.foo.com", 0, 0, 0, 0, 0,
    "v1.2.3", "com.mydomain.myapp", "[test]"
  )

  @Test
  fun `starting a stream sends headers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedHeaders = mapOf(
      "key_1" to listOf("value_a"),
      ":method" to listOf("POST"),
      ":scheme" to listOf("https"),
      ":authority" to listOf("www.envoyproxy.io"),
      ":path" to listOf("/test")
    )
    envoy.start(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .setHeaders(mapOf("key_1" to listOf("value_a")))
        .build(),
      ResponseHandler(Executor {})
    )

    verify(stream).sendHeaders(expectedHeaders, false)
  }

  @Test
  fun `sending data on stream passes data to the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val emitter = envoy.start(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      ResponseHandler(Executor {})
    )

    val data = ByteBuffer.allocate(0)

    emitter.sendData(data)

    verify(stream).sendData(data, false)
  }

  @Test
  fun `closing stream with data sends empty data to the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val emitter = envoy.start(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      ResponseHandler(Executor {})
    )

    emitter.close(ByteBuffer.allocate(0))

    verify(stream).sendData(ByteBuffer.allocate(0), true)
  }

  @Test
  fun `closing stream with trailers sends trailers to the underlying stream `() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val trailers = mapOf("key_1" to listOf("value_a"))
    val emitter = envoy.start(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      ResponseHandler(Executor {})
    )

    emitter.close(trailers)

    verify(stream).sendTrailers(trailers)
  }

  @Test
  fun `sending unary headers only request closes stream with headers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedHeaders = mapOf(
      "key_1" to listOf("value_a"),
      ":method" to listOf("POST"),
      ":scheme" to listOf("https"),
      ":authority" to listOf("www.envoyproxy.io"),
      ":path" to listOf("/test")
    )
    envoy.send(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .setHeaders(mapOf("key_1" to listOf("value_a")))
        .build(),
      null,
      null,
      ResponseHandler(Executor {})
    )

    verify(stream).sendHeaders(expectedHeaders, true)
  }

  @Test
  fun `sending unary headers and data with no trailers closes stream with data`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedBody = ByteBuffer.allocate(0)
    envoy.send(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      expectedBody,
      null,
      ResponseHandler(Executor {})
    )

    verify(stream).sendData(expectedBody, true)
  }

  @Test
  fun `sending unary trailers closes stream with trailers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedBody = ByteBuffer.allocate(0)
    val expectedTrailers = mapOf("key_1" to listOf("value_a"))
    envoy.send(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      expectedBody,
      expectedTrailers,
      ResponseHandler(Executor {})
    )

    verify(stream).sendData(expectedBody, false)
    verify(stream).sendTrailers(expectedTrailers)
  }

  @Test
  fun `cancelling stream cancels the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val emitter = envoy.send(
      RequestBuilder(
        method = RequestMethod.POST,
        scheme = "https",
        authority = "www.envoyproxy.io",
        path = "/test"
      )
        .build(),
      ByteBuffer.allocate(0),
      mapOf("key_1" to listOf("value_a")),
      ResponseHandler(Executor {})
    )

    emitter.cancel()

    verify(stream).cancel()
  }
}

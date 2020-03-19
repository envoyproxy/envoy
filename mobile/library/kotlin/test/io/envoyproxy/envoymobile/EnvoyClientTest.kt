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
  private val config = EnvoyConfiguration("stats.foo.com", 0, 0, 0, 0, 0, "v1.2.3", "com.mydomain.myapp")

  @Test
  fun `starting a stream on envoy sends headers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedHeaders = mapOf(
        "key_1" to listOf("value_a"),
        ":method" to listOf("POST"),
        ":scheme" to listOf("https"),
        ":authority" to listOf("api.foo.com"),
        ":path" to listOf("foo")
    )
    envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .setHeaders(mapOf("key_1" to listOf("value_a")))
            .build(),
        ResponseHandler(Executor {}))

    verify(stream).sendHeaders(expectedHeaders, false)
  }

  @Test
  fun `sending data on stream stream forwards data to the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val emitter = envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ResponseHandler(Executor {}))

    val data = ByteBuffer.allocate(0)

    emitter.sendData(data)

    verify(stream).sendData(data, false)
  }

  @Test
  fun `sending metadata on stream forwards metadata to the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val metadata = mapOf("key_1" to listOf("value_a"))
    val emitter = envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ResponseHandler(Executor {}))

    emitter.sendMetadata(metadata)

    verify(stream).sendMetadata(metadata)
  }

  @Test
  fun `closing stream sends empty data to the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val emitter = envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ResponseHandler(Executor {}))

    emitter.close(null)

    verify(stream).sendData(ByteBuffer.allocate(0), true)
  }

  @Test
  fun `closing stream with trailers sends trailers to the underlying stream `() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val trailers = mapOf("key_1" to listOf("value_a"))
    val emitter = envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ResponseHandler(Executor {}))

    emitter.close(trailers)

    verify(stream).sendTrailers(trailers)
  }

  @Test
  fun `sending request on envoy sends headers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val expectedHeaders = mapOf(
        "key_1" to listOf("value_a"),
        ":method" to listOf("POST"),
        ":scheme" to listOf("https"),
        ":authority" to listOf("api.foo.com"),
        ":path" to listOf("foo")
    )
    envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .setHeaders(mapOf("key_1" to listOf("value_a")))
            .build(),
        ByteBuffer.allocate(0),
        ResponseHandler(Executor {}))

    verify(stream).sendHeaders(expectedHeaders, false)
  }

  @Test
  fun `sending request on envoy passes the body buffer`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val body = ByteBuffer.allocate(0)
    envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        body,
        ResponseHandler(Executor {}))

    verify(stream).sendData(body, false)
  }

  @Test
  fun `sending request on envoy without trailers sends empty trailers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val body = ByteBuffer.allocate(0)
    envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        body,
        ResponseHandler(Executor {}))

    verify(stream).sendTrailers(emptyMap())
  }

  @Test
  fun `sending request on envoy sends trailers`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val trailers = mapOf("key_1" to listOf("value_a"))
    envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ByteBuffer.allocate(0),
        trailers,
        ResponseHandler(Executor {}))

    verify(stream).sendTrailers(trailers)
  }

  @Test
  fun `cancelling stream cancels the underlying stream`() {
    `when`(engine.startStream(any())).thenReturn(stream)
    val envoy = Envoy(engine, config)

    val trailers = mapOf("key_1" to listOf("value_a"))
    val emitter = envoy.send(
        RequestBuilder(
            method = RequestMethod.POST,
            scheme = "https",
            authority = "api.foo.com",
            path = "foo")
            .build(),
        ByteBuffer.allocate(0),
        trailers,
        ResponseHandler(Executor {}))

    emitter.cancel()

    verify(stream).cancel()
  }
}

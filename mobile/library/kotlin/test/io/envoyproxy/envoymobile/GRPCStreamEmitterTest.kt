package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.After
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

class GRPCStreamEmitterTest {

  // TODO: Problems with nhaarman/mockito-kotlin https://github.com/lyft/envoy-mobile/issues/504
  // This is a total hack to get something to work.
  private lateinit var emitter: StreamEmitter

  private val dataOutputStream = ByteArrayOutputStream()
  private var isClosedCallWithEmptyData = false

  @Before
  fun setup() {
    emitter = object : StreamEmitter {
      override fun cancel() {
        throw UnsupportedOperationException("unexpected usage of mock emitter")
      }

      override fun sendData(byteBuffer: ByteBuffer): StreamEmitter {
        dataOutputStream.write(byteBuffer.array())
        return this
      }

      override fun close(trailers: Map<String, List<String>>) {
        throw UnsupportedOperationException("unexpected usage of mock emitter")
      }

      override fun close(byteBuffer: ByteBuffer) {
        isClosedCallWithEmptyData = byteBuffer.array().size == 0
      }
    }
  }

  @After
  fun teardown() {
    dataOutputStream.reset()
    isClosedCallWithEmptyData = false
  }

  @Test
  fun `prefix and data is sent on send data`() {
    val payload = "data".toByteArray(Charsets.UTF_8)
    val message = ByteBuffer.wrap(payload)
    GRPCStreamEmitter(emitter)
      .sendMessage(message)

    assertThat(dataOutputStream.toByteArray()).hasSize(payload.size + GRPC_PREFIX_LENGTH)
  }

  @Test
  fun `compression flag is set on the first bit of the prefix`() {
    val payload = "data".toByteArray(Charsets.UTF_8)
    val message = ByteBuffer.wrap(payload)
    GRPCStreamEmitter(emitter)
      .sendMessage(message)

    assertThat(dataOutputStream.toByteArray()[0]).isEqualTo(0)
  }

  @Test
  fun `message length is set on the 1-4 bytes of the prefix`() {
    val payload = "data".toByteArray(Charsets.UTF_8)
    val message = ByteBuffer.wrap(payload)
    GRPCStreamEmitter(emitter)
      .sendMessage(message)

    assertThat(ByteBuffer.wrap(dataOutputStream.toByteArray().sliceArray(1..4)).order(ByteOrder.BIG_ENDIAN).int).isEqualTo(payload.size)
  }

  @Test
  fun `message is sent after the prefix`() {
    val payload = "data".toByteArray(Charsets.UTF_8)
    val message = ByteBuffer.wrap(payload)
    GRPCStreamEmitter(emitter)
      .sendMessage(message)

    assertThat(dataOutputStream.toByteArray().sliceArray(GRPC_PREFIX_LENGTH until dataOutputStream.size()).toString(Charsets.UTF_8)).isEqualTo("data")

  }

  @Test
  fun `close is called with empty data frame`() {
    GRPCStreamEmitter(emitter)
      .close()

    assertThat(isClosedCallWithEmptyData).isTrue()
  }
}

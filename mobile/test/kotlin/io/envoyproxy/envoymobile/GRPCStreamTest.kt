package io.envoyproxy.envoymobile

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.mocks.MockStream
import io.envoyproxy.envoymobile.mocks.MockStreamClient
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class GRPCStreamTest {
  private val message1 = ByteBuffer.wrap(byteArrayOf(0x0, 0x1, 0x2, 0x3, 0x4, 0x5))

  // Request tests

  @Test
  fun `data size is five bytes greater than message size`() {
    val sentData = ByteArrayOutputStream()
    val streamClient = MockStreamClient { stream ->
      stream.onRequestData = { data, _ -> sentData.write(data.array()) }
    }

    GRPCClient(streamClient).newGRPCStreamPrototype().start().sendMessage(message1)

    assertThat(sentData.size()).isEqualTo(5 + message1.array().count())
  }

  @Test
  fun `prefixes sent data with zero compression flag`() {
    val sentData = ByteArrayOutputStream()
    val streamClient = MockStreamClient { stream ->
      stream.onRequestData = { data, _ -> sentData.write(data.array()) }
    }

    GRPCClient(streamClient).newGRPCStreamPrototype().start().sendMessage(message1)

    assertThat(sentData.toByteArray()[0]).isEqualTo(0)
  }

  @Test
  fun `prefixes sent data with big endian length of message`() {
    val sentData = ByteArrayOutputStream()
    val streamClient = MockStreamClient { stream ->
      stream.onRequestData = { data, _ -> sentData.write(data.array()) }
    }

    GRPCClient(streamClient).newGRPCStreamPrototype().start().sendMessage(message1)

    val size =
      ByteBuffer.wrap(sentData.toByteArray().sliceArray(1 until 5)).order(ByteOrder.BIG_ENDIAN).int
    assertThat(size).isEqualTo(message1.array().count())
  }

  @Test
  fun `appends message data at the end of sent data`() {
    val sentData = ByteArrayOutputStream()
    val streamClient = MockStreamClient { stream ->
      stream.onRequestData = { data, _ -> sentData.write(data.array()) }
    }

    GRPCClient(streamClient).newGRPCStreamPrototype().start().sendMessage(message1)

    assertThat(sentData.toByteArray().sliceArray(5 until sentData.size()))
      .isEqualTo(message1.array())
  }

  @Test
  fun `cancel calls a stream callback`() {
    val countDownLatch = CountDownLatch(1)
    val streamClient = MockStreamClient { stream ->
      stream.onCancel = { countDownLatch.countDown() }
    }

    GRPCClient(streamClient).newGRPCStreamPrototype().start().cancel()

    assertThat(countDownLatch.await(2000, TimeUnit.MILLISECONDS)).isTrue()
  }

  // Response tests

  @Test(timeout = 1000L)
  fun `headers callback passes headers`() {
    val countDownLatch = CountDownLatch(1)
    val expectedHeaders =
      ResponseHeaders(mapOf("grpc-status" to listOf("1"), "x-other" to listOf("foo", "bar")))
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseHeaders { headers, endStream, _ ->
        assertThat(headers.caseSensitiveHeaders()).isEqualTo(expectedHeaders.caseSensitiveHeaders())
        assertThat(endStream).isTrue()
        countDownLatch.countDown()
      }
      .start()

    stream?.receiveHeaders(expectedHeaders, true)
    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `trailers callback passes trailers`() {
    val countDownLatch = CountDownLatch(1)
    val expectedTrailers =
      ResponseTrailers(mapOf("x-foo" to listOf("bar"), "x-baz" to listOf("1", "2")))
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseTrailers { trailers, _ ->
        assertThat(trailers.caseSensitiveHeaders())
          .isEqualTo(expectedTrailers.caseSensitiveHeaders())
        countDownLatch.countDown()
      }
      .start()

    stream?.receiveTrailers(expectedTrailers)
    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `message callback buffers data sent in single chunk`() {
    val countDownLatch = CountDownLatch(1)
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message, _ ->
        assertThat(message.array()).isEqualTo(message1.array())
        countDownLatch.countDown()
      }
      .start()

    val messageLength = message1.array().count()
    val data = ByteBuffer.allocate(5 + messageLength)
    data.put(0) // Compression flag
    data.order(ByteOrder.BIG_ENDIAN)
    data.putInt(messageLength) // Length bytes
    data.put(message1)
    stream?.receiveData(data, false)
    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `message callback buffers data sent in multiple chunks`() {
    val countDownLatch = CountDownLatch(2)
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    val firstMessage = byteArrayOf(0x1, 0x2, 0x3, 0x4, 0x5)
    val firstMessageBuffer =
      ByteBuffer.wrap(
        byteArrayOf(
          0x0, // Compression flag
          0x0,
          0x0,
          0x0,
          0x5 // Length bytes
        ) + firstMessage
      )

    val secondMessage = byteArrayOf(0x6, 0x7, 0x8, 0x9, 0x0, 0x1)
    val secondMessageBufferPart1 =
      ByteBuffer.wrap(
        byteArrayOf(
          0x0, // Compression flag
          0x0,
          0x0,
          0x0,
          0x6 // Length bytes
        ) + secondMessage.sliceArray(0 until 2)
      )
    val secondMessageBufferPart2 =
      ByteBuffer.wrap(secondMessage.sliceArray(2 until secondMessage.count()))

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message, _ ->
        if (countDownLatch.count == 2L) {
          assertThat(message.array()).isEqualTo(firstMessage)
        } else {
          assertThat(message.array()).isEqualTo(secondMessage)
        }
        countDownLatch.countDown()
      }
      .start()

    stream?.receiveData(firstMessageBuffer, false)
    stream?.receiveData(secondMessageBufferPart1, false)
    stream?.receiveData(secondMessageBufferPart2, false)
    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `message callback can be called with zero length message`() {
    val countDownLatch = CountDownLatch(1)
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message, _ ->
        assertThat(message.array()).hasLength(0)
        countDownLatch.countDown()
      }
      .start()

    val emptyMessage =
      ByteBuffer.wrap(
        byteArrayOf(
          0x0, // Compression flag
          0x0,
          0x0,
          0x0,
          0x0 // Length bytes
        )
      )

    stream?.receiveData(emptyMessage, false)
    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `message callback can be called with message after zero length message`() {
    val countDownLatch = CountDownLatch(2)
    var stream: MockStream? = null
    val streamClient = MockStreamClient { stream = it }

    val emptyMessageBuffer =
      ByteBuffer.wrap(
        byteArrayOf(
          0x0, // Compression flag
          0x0,
          0x0,
          0x0,
          0x0 // Length bytes
        )
      )

    val secondMessage = byteArrayOf(0x6, 0x7, 0x8, 0x9, 0x0, 0x1)
    val secondMessageBuffer =
      ByteBuffer.wrap(
        byteArrayOf(
          0x0, // Compression flag
          0x0,
          0x0,
          0x0,
          0x6 // Length bytes
        ) + secondMessage
      )

    GRPCClient(streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message, _ ->
        if (countDownLatch.count == 2L) {
          assertThat(message.array()).hasLength(0)
        } else {
          assertThat(message.array()).isEqualTo(secondMessage)
        }
        countDownLatch.countDown()
      }
      .start()

    stream?.receiveData(emptyMessageBuffer, false)
    stream?.receiveData(secondMessageBuffer, false)
    countDownLatch.await()
  }
}

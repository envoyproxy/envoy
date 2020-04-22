package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor

class GRPCResponseHandlerTest {

  @Test(timeout = 1000L)
  fun `headers and grpc status is passed to on headers`() {
    val countDownLatch = CountDownLatch(1)

    val handler = GRPCResponseHandler(Executor { })
        .onHeaders { headers, grpcStatus ->
          assertThat(headers).isEqualTo(
            mapOf("grpc-status" to listOf("1"), "other" to listOf("foo", "bar"))
          )
          assertThat(grpcStatus).isEqualTo(1)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onHeaders(
        mapOf("grpc-status" to listOf("1"), "other" to listOf("foo", "bar")),
        true)

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `trailers are passed on trailers`() {
    val countDownLatch = CountDownLatch(1)

    val handler = GRPCResponseHandler(Executor { })
        .onTrailers { trailers ->
          assertThat(trailers).isEqualTo(mapOf("foo" to listOf("bar"), "baz" to listOf("1", "2")))
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onTrailers(
        mapOf("foo" to listOf("bar"), "baz" to listOf("1", "2")))

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `messages are passed when it fits in the same chunk`() {
    val countDownLatch = CountDownLatch(1)

    val data = "data".toByteArray(Charsets.UTF_8)

    val prefix = ByteBuffer.allocate(5)
    prefix.put(0)
    prefix.order(ByteOrder.BIG_ENDIAN)
    prefix.putInt(data.size)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix.array())
    outputStream.write(data)
    val byteBuffer = ByteBuffer.wrap(outputStream.toByteArray())

    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          assertThat(message.array().toString(Charsets.UTF_8)).isEqualTo("data")
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(byteBuffer, true)

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `messages are buffered and passed until all chunks are received`() {
    val countDownLatch = CountDownLatch(1)

    val part1 = "data".toByteArray(Charsets.UTF_8)
    val part2 = "_by_".toByteArray(Charsets.UTF_8)
    val part3 = "parts".toByteArray(Charsets.UTF_8)

    val prefix = ByteBuffer.allocate(5)
    prefix.put(0)
    prefix.order(ByteOrder.BIG_ENDIAN)
    prefix.putInt(part1.size + part2.size + part3.size)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix.array())
    outputStream.write(part1)

    val initialBuffer = ByteBuffer.wrap(outputStream.toByteArray())

    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          assertThat(message.array().toString(Charsets.UTF_8)).isEqualTo("data_by_parts")
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(initialBuffer, false)
    handler.underlyingHandler.underlyingCallbacks.onData(ByteBuffer.wrap(part2), false)
    handler.underlyingHandler.underlyingCallbacks.onData(ByteBuffer.wrap(part3), true)

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `multiple messages in the same chunk will be passed down`() {
    val countDownLatch = CountDownLatch(2)

    val part1 = "part1".toByteArray(Charsets.UTF_8)
    val part2 = "part2".toByteArray(Charsets.UTF_8)

    val prefix1 = ByteBuffer.allocate(5)
    prefix1.put(0)
    prefix1.order(ByteOrder.BIG_ENDIAN)
    prefix1.putInt(part1.size)

    val prefix2 = ByteBuffer.allocate(5)
    prefix2.put(0)
    prefix2.order(ByteOrder.BIG_ENDIAN)
    prefix2.putInt(part2.size)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix1.array())
    outputStream.write(part1)
    outputStream.write(prefix2.array())
    outputStream.write(part2)

    val part1AndPart2 = ByteBuffer.wrap(outputStream.toByteArray())

    val messages = mutableListOf<ByteBuffer>()
    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          messages.add(message)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(part1AndPart2, false)

    countDownLatch.await()
    assertThat(messages[0].array().toString(Charsets.UTF_8)).isEqualTo("part1")
    assertThat(messages[1].array().toString(Charsets.UTF_8)).isEqualTo("part2")
  }

  @Test(timeout = 1000L)
  fun `multiple messages in different chunks will be passed down`() {
    val countDownLatch = CountDownLatch(2)

    val part1 = "part1".toByteArray(Charsets.UTF_8)
    val part2a = "part2a".toByteArray(Charsets.UTF_8)
    val part2b = "_part2b".toByteArray(Charsets.UTF_8)

    val prefix1 = ByteBuffer.allocate(5)
    prefix1.put(0)
    prefix1.order(ByteOrder.BIG_ENDIAN)
    prefix1.putInt(part1.size)

    val prefix2 = ByteBuffer.allocate(5)
    prefix2.put(0)
    prefix2.order(ByteOrder.BIG_ENDIAN)
    prefix2.putInt(part2a.size + part2b.size)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix1.array())
    outputStream.write(part1)
    outputStream.write(prefix2.array())
    outputStream.write(part2a)
    val part1AndPart2a = ByteBuffer.wrap(outputStream.toByteArray())

    val messages = mutableListOf<ByteBuffer>()
    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          messages.add(message)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(part1AndPart2a, false)
    handler.underlyingHandler.underlyingCallbacks.onData(ByteBuffer.wrap(part2b), false)

    countDownLatch.await()
    assertThat(messages[0].array().toString(Charsets.UTF_8)).isEqualTo("part1")
    assertThat(messages[1].array().toString(Charsets.UTF_8)).isEqualTo("part2a_part2b")
  }

  @Test(timeout = 1000L)
  fun `empty messages in same will send empty message down`() {
    val countDownLatch = CountDownLatch(2)

    val part2 = "part2".toByteArray(Charsets.UTF_8)

    val prefix1 = ByteBuffer.allocate(5)
    prefix1.put(0)
    prefix1.order(ByteOrder.BIG_ENDIAN)
    prefix1.putInt(0)

    val prefix2 = ByteBuffer.allocate(5)
    prefix2.put(0)
    prefix2.order(ByteOrder.BIG_ENDIAN)
    prefix2.putInt(part2.size)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix1.array())
    outputStream.write(prefix2.array())
    outputStream.write(part2)
    val resultMessages = ByteBuffer.wrap(outputStream.toByteArray())

    val messages = mutableListOf<ByteBuffer>()
    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          messages.add(message)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(resultMessages, false)

    countDownLatch.await()
    assertThat(messages[0].array()).isEmpty()
    assertThat(messages[1].array().toString(Charsets.UTF_8)).isEqualTo("part2")
  }


  @Test(timeout = 1000L)
  fun `zero length messages are passed as empty byte buffers`() {
    val countDownLatch = CountDownLatch(1)
    val prefix = ByteBuffer.allocate(5)
    prefix.put(0)
    prefix.order(ByteOrder.BIG_ENDIAN)
    prefix.putInt(0)

    val outputStream = ByteArrayOutputStream()
    outputStream.write(prefix.array())
    val data = ByteBuffer.wrap(outputStream.toByteArray())

    val messages = mutableListOf<ByteBuffer>()
    val handler = GRPCResponseHandler(Executor { })
        .onMessage { message ->
          messages.add(message)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onData(data, false)

    countDownLatch.await()
    assertThat(messages[0].array()).isEmpty()

  }

  @Test(timeout = 1000L)
  fun `first grpc status is passed on headers`() {
    val countDownLatch = CountDownLatch(1)

    val handler = GRPCResponseHandler(Executor { })
        .onHeaders { _, grpcStatus ->
          assertThat(grpcStatus).isEqualTo(1)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks
      .onHeaders(mapOf("grpc-status" to listOf("1", "2")), true)

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `missing grpc status will default status to 0`() {
    val countDownLatch = CountDownLatch(1)

    val handler = GRPCResponseHandler(Executor { })
        .onHeaders { _, grpcStatus ->
          assertThat(grpcStatus).isEqualTo(0)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks.onHeaders(mapOf(), true)

    countDownLatch.await()
  }

  @Test(timeout = 1000L)
  fun `invalid grpc status will default status to 0`() {
    val countDownLatch = CountDownLatch(1)

    val handler = GRPCResponseHandler(Executor { })
        .onHeaders { _, grpcStatus ->
          assertThat(grpcStatus).isEqualTo(0)
          countDownLatch.countDown()
        }

    handler.underlyingHandler.underlyingCallbacks
      .onHeaders(mapOf("grpc-status" to listOf("invalid")), true)

    countDownLatch.await()
  }
}

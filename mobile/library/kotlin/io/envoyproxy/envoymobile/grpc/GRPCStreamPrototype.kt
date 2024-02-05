package io.envoyproxy.envoymobile

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executor
import java.util.concurrent.Executors

/**
 * A type representing a gRPC stream that has not yet been started.
 *
 * Constructed via `GRPCClient`, and used to assign response callbacks prior to starting a
 * `GRPCStream` by calling `start()`.
 */
class GRPCStreamPrototype(private val underlyingStream: StreamPrototype) {
  /**
   * Start a new gRPC stream.
   *
   * @param executor Executor on which to receive callback events.
   * @return The new gRPC stream.
   */
  fun start(executor: Executor = Executors.newSingleThreadExecutor()): GRPCStream {
    val stream = underlyingStream.start(executor)
    return GRPCStream(stream)
  }

  /**
   * Specify a callback for when response headers are received by the stream.
   *
   * @param closure Closure which will receive the headers and flag indicating if the stream is
   *   headers-only.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseHeaders(
    closure: (headers: ResponseHeaders, endStream: Boolean, streamIntel: StreamIntel) -> Unit
  ): GRPCStreamPrototype {
    underlyingStream.setOnResponseHeaders(closure)
    return this
  }

  /**
   * Specify a callback for when a new message has been received by the stream. If `endStream` is
   * `true`, the stream is complete.
   *
   * @param closure Closure which will receive messages on the stream.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseMessage(
    closure: (data: ByteBuffer, streamIntel: StreamIntel) -> Unit
  ): GRPCStreamPrototype {
    val byteBufferedOutputStream = ByteArrayOutputStream()
    val processor = GRPCMessageProcessor()
    var processState: GRPCMessageProcessor.ProcessState =
      GRPCMessageProcessor.ProcessState.CompressionFlag
    underlyingStream.setOnResponseData { byteBuffer, _, streamIntel ->
      val byteBufferArray =
        if (byteBuffer.hasArray()) {
          byteBuffer.array()
        } else {
          val array = ByteArray(byteBuffer.remaining())
          byteBuffer.get(array)
          array
        }
      byteBufferedOutputStream.write(byteBufferArray)

      processState =
        processor.processData(byteBufferedOutputStream, processState, streamIntel, closure)
    }

    return this
  }

  /**
   * Specify a callback for when trailers are received by the stream. If the closure is called, the
   * stream is complete.
   *
   * @param closure Closure which will receive the trailers.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseTrailers(
    closure: (trailers: ResponseTrailers, streamIntel: StreamIntel) -> Unit
  ): GRPCStreamPrototype {
    underlyingStream.setOnResponseTrailers(closure)
    return this
  }

  /**
   * Specify a callback for when an internal Envoy exception occurs with the stream. If the closure
   * is called, the stream is complete.
   *
   * @param closure Closure which will be called when an error occurs.
   * @return This stream, for chaining syntax.
   */
  fun setOnError(
    closure: (error: EnvoyError, finalStreamIntel: FinalStreamIntel) -> Unit
  ): GRPCStreamPrototype {
    underlyingStream.setOnError(closure)
    return this
  }

  /**
   * Specify a callback for when the stream is canceled. If the closure is called, the stream is
   * complete.
   *
   * @param closure Closure which will be called when the stream is canceled.
   * @return This stream, for chaining syntax.
   */
  fun setOnCancel(closure: (finalStreamIntel: FinalStreamIntel) -> Unit): GRPCStreamPrototype {
    underlyingStream.setOnCancel(closure)
    return this
  }
}

private class GRPCMessageProcessor {
  /** Represents the process state of the response stream's body data. */
  sealed class ProcessState {
    // Awaiting a gRPC compression flag.
    object CompressionFlag : ProcessState()

    // Awaiting the length specification of the next message.
    object MessageLength : ProcessState()

    // Awaiting a message with the specified length.
    class Message(val messageLength: Int) : ProcessState()
  }

  /**
   * Recursively processes a buffer of data, buffering it into messages based on state. When a
   * message has been fully buffered, `onMessage` will be called with the message.
   *
   * @param bufferedStream The buffer of data from which to determine state and messages.
   * @param processState The current process state of the buffering.
   * @param onMessage Closure to call when a new message is available.
   * @return The state after processing the passed data.
   */
  fun processData(
    bufferedStream: ByteArrayOutputStream,
    processState: GRPCMessageProcessor.ProcessState,
    streamIntel: StreamIntel,
    onMessage: (byteBuffer: ByteBuffer, streamIntel: StreamIntel) -> Unit
  ): GRPCMessageProcessor.ProcessState {
    var nextState = processState

    when (processState) {
      is ProcessState.CompressionFlag -> {
        val byteArray = bufferedStream.toByteArray()
        if (byteArray.isEmpty()) {
          // We don't have enough information to extract the compression flag, so we'll just return
          return ProcessState.CompressionFlag
        }

        val compressionFlag = byteArray[0]
        // TODO: Support gRPC compression https://github.com/envoyproxy/envoy-mobile/issues/501.
        if (compressionFlag.compareTo(0) != 0) {
          bufferedStream.reset()
        }

        nextState = ProcessState.MessageLength
      }
      is ProcessState.MessageLength -> {
        if (bufferedStream.size() < GRPC_PREFIX_LENGTH) {
          // We don't have enough information to extract the message length, so we'll just return
          return ProcessState.MessageLength
        }

        val byteArray = bufferedStream.toByteArray()
        val buffer = ByteBuffer.wrap(byteArray.sliceArray(1..4))
        buffer.order(ByteOrder.BIG_ENDIAN)
        val messageLength = buffer.int
        nextState = ProcessState.Message(messageLength)
      }
      is ProcessState.Message -> {
        if (bufferedStream.size() < processState.messageLength + GRPC_PREFIX_LENGTH) {
          // We don't have enough bytes to construct the message, so we'll just return
          return ProcessState.Message(processState.messageLength)
        }

        val byteArray = bufferedStream.toByteArray()
        onMessage(
          ByteBuffer.wrap(
            byteArray.sliceArray(
              GRPC_PREFIX_LENGTH until GRPC_PREFIX_LENGTH + processState.messageLength
            )
          ),
          streamIntel
        )
        bufferedStream.reset()
        bufferedStream.write(
          byteArray.sliceArray(GRPC_PREFIX_LENGTH + processState.messageLength until byteArray.size)
        )

        val remainingLength = GRPC_PREFIX_LENGTH + processState.messageLength until byteArray.size
        if (byteArray.sliceArray(remainingLength).isEmpty()) {
          return ProcessState.CompressionFlag
        } else {
          nextState = ProcessState.CompressionFlag
        }
      }
    }

    return processData(bufferedStream, nextState, streamIntel, onMessage)
  }
}

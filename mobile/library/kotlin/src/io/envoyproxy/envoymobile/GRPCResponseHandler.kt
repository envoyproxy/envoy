package io.envoyproxy.envoymobile

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executor


class GRPCResponseHandler(
    val executor: Executor
) {

  /**
   * Represents the process state of the response stream's body data.
   */
  private sealed class ProcessState {
    // Awaiting a gRPC compression flag.
    object CompressionFlag : ProcessState()

    // Awaiting the length specification of the next message.
    object MessageLength : ProcessState()

    // Awaiting a message with the specified length.
    class Message(val messageLength: Int) : ProcessState()
  }

  internal val underlyingHandler: ResponseHandler = ResponseHandler(executor)
  private var errorClosure: (error: EnvoyError) -> Unit = { }

  /**
   * Specify a callback for when response headers are received by the stream.
   *
   * @param closure: Closure which will receive the headers, status code,
   *                 and flag indicating if the stream is complete.
   * @return GRPCResponseHandler, this GRPCResponseHandler.
   */
  fun onHeaders(closure: (headers: Map<String, List<String>>, statusCode: Int) -> Unit): GRPCResponseHandler {
    underlyingHandler.onHeaders { headers, _, _ ->
      val grpcStatus = headers["grpc-status"]?.first()?.toIntOrNull() ?: 0
      closure(headers, grpcStatus)
    }
    return this
  }

  /**
   * Specify a callback for when a data frame is received by the stream.
   *
   * @param closure: Closure which will receive the data,
   *                 and flag indicating if the stream is complete.
   * @return GRPCResponseHandler, this GRPCResponseHandler.
   */
  fun onMessage(closure: (byteBuffer: ByteBuffer) -> Unit): GRPCResponseHandler {
    val byteBufferedOutputStream = ByteArrayOutputStream()
    var processState: ProcessState = ProcessState.CompressionFlag
    underlyingHandler.onData { byteBuffer, _ ->

      val byteBufferArray = if (byteBuffer.hasArray()) {
        byteBuffer.array()
      } else {
        val array = ByteArray(byteBuffer.remaining())
        byteBuffer.get(array)
        array
      }
      byteBufferedOutputStream.write(byteBufferArray)

      processState = processData(byteBufferedOutputStream, processState, closure)
    }

    return this
  }

  /**
   * Specify a callback for when trailers are received by the stream.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will receive the trailers.
   * @return GRPCResponseHandler, this GRPCResponseHandler.
   */
  fun onTrailers(closure: (trailers: Map<String, List<String>>) -> Unit): GRPCResponseHandler {
    underlyingHandler.onTrailers(closure)
    return this
  }

  /**
   * Specify a callback for when an internal Envoy exception occurs with the stream.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will be called when an error occurs.
   * @return GRPCResponseHandler, this GRPCResponseHandler.
   */
  fun onError(closure: (error: EnvoyError) -> Unit): GRPCResponseHandler {
    this.errorClosure = closure
    underlyingHandler.onError(closure)
    return this
  }

  /**
   * Recursively processes a buffer of data, buffering it into messages based on state.
   * When a message has been fully buffered, `onMessage` will be called with the message.
   *
   * @param bufferedStream The buffer of data from which to determine state and messages.
   * @param processState The current process state of the buffering.
   * @param onMessage Closure to call when a new message is available.
   */
  private fun processData(
      bufferedStream: ByteArrayOutputStream,
      processState: ProcessState,
      onMessage: (byteBuffer: ByteBuffer) -> Unit): ProcessState {

    var nextState = processState

    when (processState) {
      is ProcessState.CompressionFlag -> {
        val byteArray = bufferedStream.toByteArray()
        if (byteArray.isEmpty()) {
          // We don't have enough information to extract the compression flag, so we'll just return
          return ProcessState.CompressionFlag
        }

        val compressionFlag = byteArray[0]
        // TODO: Support gRPC compression https://github.com/lyft/envoy-mobile/issues/501
        if (compressionFlag.compareTo(0) != 0) {
          errorClosure(EnvoyError(0, "Unable to read compressed gRPC response message"))

          // no op the current onData and clean up
          errorClosure = { }
          underlyingHandler.onHeaders { _, _, _ -> }
          underlyingHandler.onData { _, _ -> }
          underlyingHandler.onTrailers { }
          underlyingHandler.onError { }
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
        onMessage(ByteBuffer.wrap(
            byteArray.sliceArray(GRPC_PREFIX_LENGTH until GRPC_PREFIX_LENGTH + processState.messageLength)))
        bufferedStream.reset()
        bufferedStream.write(
            byteArray.sliceArray(GRPC_PREFIX_LENGTH + processState.messageLength until byteArray.size))

        if (byteArray.sliceArray(GRPC_PREFIX_LENGTH + processState.messageLength until byteArray.size).isEmpty()) {
          return ProcessState.CompressionFlag
        } else {
          nextState = ProcessState.CompressionFlag
        }
      }
    }

    return processData(bufferedStream, nextState, onMessage)
  }
}

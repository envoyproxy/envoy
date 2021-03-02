package io.envoyproxy.envoymobile

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * A type representing a gRPC stream that is actively transferring data.
 *
 * Constructed using `GRPCStreamPrototype`, and used to write to the network.
 */
class GRPCStream(
  private val underlyingStream: Stream
) {
  /**
   * Send headers over the gRPC stream.
   *
   * @param headers Headers to send over the stream.
   * @param endStream Whether this is a headers-only request.
   * @return This stream, for chaining syntax.
   */
  fun sendHeaders(headers: GRPCRequestHeaders, endStream: Boolean): GRPCStream {
    underlyingStream.sendHeaders(headers as RequestHeaders, endStream)
    return this
  }

  /**
   * Send a protobuf message's binary data over the gRPC stream.
   *
   * @param messageData Binary data of a protobuf message to send.
   * @return This stream, for chaining syntax.
   */
  fun sendMessage(messageData: ByteBuffer): GRPCStream {
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests
    // Length-Prefixed-Message = Compressed-Flag | Message-Length | Message
    // Compressed-Flag = 0 / 1, encoded as 1 byte unsigned integer
    // Message-Length = length of Message, encoded as 4 byte unsigned integer (big endian)
    // Message = binary representation of protobuf messageData
    val byteBuffer = ByteBuffer.allocate(GRPC_PREFIX_LENGTH)

    // Compression flag (1 byte) - 0, not compressed
    byteBuffer.put(0)

    // Message length
    val messageLength = messageData.remaining()
    byteBuffer.order(ByteOrder.BIG_ENDIAN)
    byteBuffer.putInt(messageLength)

    underlyingStream.sendData(byteBuffer)
    underlyingStream.sendData(messageData)
    return this
  }

  /**
   * Close this connection.
   */
  fun close() {
    underlyingStream.close(ByteBuffer.allocate(0))
  }
}

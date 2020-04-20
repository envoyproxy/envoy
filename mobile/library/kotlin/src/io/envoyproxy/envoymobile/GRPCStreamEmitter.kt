package io.envoyproxy.envoymobile

import java.nio.ByteBuffer
import java.nio.ByteOrder

class GRPCStreamEmitter(
    private val emitter: StreamEmitter
) {
  /**
   * Send a protobuf messageData's binary data over the gRPC stream.
   *
   * @param messageData Binary data of a protobuf messageData to send.
   * @return GRPCStreamEmitter, this stream emitter.
   */
  fun sendMessage(messageData: ByteBuffer): GRPCStreamEmitter {
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

    emitter.sendData(byteBuffer)
    emitter.sendData(messageData)
    return this
  }

  /**
   * Close this connection.
   */
  fun close() {
    emitter.close(ByteBuffer.allocate(0))
  }
}

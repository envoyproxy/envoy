package io.envoyproxy.envoymobile

// 1 byte for the compression flag, 4 bytes for the message length (int)
internal const val GRPC_PREFIX_LENGTH = 5

/**
 * Client that supports sending and receiving gRPC traffic.
 *
 * @param streamClient The stream client to use for gRPC streams.
 */
class GRPCClient(private val streamClient: StreamClient) {
  /**
   * Create a new gRPC stream prototype which can be used to start streams.
   *
   * @return The new gRPC stream prototype.
   */
  fun newGRPCStreamPrototype() = GRPCStreamPrototype(streamClient.newStreamPrototype())
}

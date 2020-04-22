package io.envoyproxy.envoymobile

// 1 byte for the compression flag, 4 bytes for the message length (int)
const val GRPC_PREFIX_LENGTH = 5

/**
 * Client that supports sending and receiving gRPC traffic.
 *
 * @param httpClient The HTTP client to use for gRPC streams.
 */
class GRPCClient(
    private val httpClient: HTTPClient
) {

  /**
   * Start a gRPC request with the provided handler.
   *
   * @param request The outbound gRPC request. See `GRPCRequestBuilder` for creation.
   * @param handler Handler for receiving responses.
   *
   * @returns GRPCStreamEmitter, An emitter that can be used for sending more traffic over the stream.
   */
  fun start(request: Request, grpcResponseHandler: GRPCResponseHandler): GRPCStreamEmitter {
    val emitter = httpClient.start(request, grpcResponseHandler.underlyingHandler)
    return GRPCStreamEmitter(emitter)
  }
}

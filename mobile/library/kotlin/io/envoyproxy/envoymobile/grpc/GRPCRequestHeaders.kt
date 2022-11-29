package io.envoyproxy.envoymobile

/*
 * Headers representing an outbound gRPC request.
 */
class GRPCRequestHeaders : RequestHeaders {
  /**
   * Internal constructor used by builders.
   *
   * @param headers: Headers to set.
   */
  internal constructor(headers: Map<String, MutableList<String>>) : super(HeadersContainer(headers))

  internal constructor(container: HeadersContainer) : super(container)

  /**
   * Convert the headers back to a builder for mutation.
   *
   * @return GRPCRequestHeadersBuilder, The new builder.
   */
  fun toGRPCRequestHeadersBuilder() = GRPCRequestHeadersBuilder(container)
}

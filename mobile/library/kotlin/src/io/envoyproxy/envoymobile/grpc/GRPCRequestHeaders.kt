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
  internal constructor(headers: Map<String, List<String>>) : super(headers)

  /**
   * Convert the headers back to a builder for mutation.
   *
   * @return GRPCRequestHeadersBuilder, The new builder.
   */
  fun toGRPCRequestHeadersBuilder() = GRPCRequestHeadersBuilder(
    headers.mapValues {
      it.value.toMutableList()
    }.toMutableMap()
  )
}

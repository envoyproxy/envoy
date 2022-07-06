package io.envoyproxy.envoymobile

/*
 * Builder used for constructing instances of `GRPCRequestHeaders`.
 */
class GRPCRequestHeadersBuilder : HeadersBuilder {
  /**
   * Internal constructor used by builders.
   *
   * @param headers: Headers to set.
   */
  internal constructor(headers: Map<String, MutableList<String>>) : super(HeadersContainer(headers))

  /**
   * Instantiate a new builder.
   *
   * @param container: The headers container to start with.
   */
  internal constructor(container: HeadersContainer) : super(container)

  override fun add(name: String, value: String): GRPCRequestHeadersBuilder {
    super.add(name, value)
    return this
  }

  override fun set(name: String, value: MutableList<String>): GRPCRequestHeadersBuilder {
    super.set(name, value)
    return this
  }

  override fun remove(name: String): GRPCRequestHeadersBuilder {
    super.remove(name)
    return this
  }

  override fun internalSet(name: String, value: MutableList<String>): GRPCRequestHeadersBuilder {
    super.internalSet(name, value)
    return this
  }

  /**
   * Initialize a new builder.
   *
   * @param scheme The URL scheme for the request (i.e., "https").
   * @param authority The URL authority for the request (i.e., "api.foo.com").
   * @param path Path for the RPC (i.e., `/pb.api.v1.Foo/GetBar`).
   */
  constructor(scheme: String, authority: String, path: String) : super(HeadersContainer(
    mapOf<String, MutableList<String>>(
      ":authority" to mutableListOf<String>(authority),
      ":method" to mutableListOf<String>("POST"),
      ":path" to mutableListOf<String>(path),
      ":scheme" to mutableListOf<String>(scheme),
      "content-type" to mutableListOf<String>("application/grpc"),
      "x-envoy-mobile-upstream-protocol" to mutableListOf<String>(UpstreamHttpProtocol.HTTP2.stringValue)
    )
  )
  )

  /**
   * Add a specific timeout for the gRPC request. This will be sent in the `grpc-timeout` header.
   *
   * @param timeoutMs Timeout, in milliseconds.
   * @return This builder.
   */
  fun addtimeoutMs(timeoutMs: Int?): GRPCRequestHeadersBuilder {
    val headerName = "grpc-timeout"
    if (timeoutMs == null) {
      remove(headerName)
    } else {
      add(headerName, "${timeoutMs}m")
    }
    return this
  }

  /**
   * Build the request headers using the current builder.
   *
   * @return New instance of request headers.
   */
  fun build(): GRPCRequestHeaders {
    return GRPCRequestHeaders(container)
  }
}

package io.envoyproxy.envoymobile

/*
 * Builder used for constructing instances of RequestHeaders`.
 */
class RequestHeadersBuilder: HeadersBuilder {
  /**
   * Initialize a new instance of the builder.
   *
   * @param method:    Method for the request.
   * @param scheme:    The URL scheme for the request (i.e., "https").
   * @param authority: The URL authority for the request (i.e., "api.foo.com").
   * @param path:      The URL path for the request (i.e., "/foo").
   */
  constructor(method: RequestMethod, scheme: String = "https",
              authority: String, path: String) :
                super(mutableMapOf(":authority" to mutableListOf(authority),
                                   ":method" to mutableListOf(method.stringValue),
                                   ":path" to mutableListOf(path),
                                   ":scheme" to mutableListOf(scheme)))

  /**
   * Instantiate a new builder. Used only by RequestHeaders to convert back to
   * RequestHeadersBuilder.
   *
   * @param headers: The headers to start with.
   */
  internal constructor(headers: MutableMap<String, MutableList<String>>) : super(headers)

  /**
   * Add a retry policy to be used with this request.
   *
   * @param retryPolicy: The retry policy to use.
   *
   * @return RequestHeadersBuilder, This builder.
   */
  fun addRetryPolicy(retryPolicy: RetryPolicy): RequestHeadersBuilder {
    for ((name, value) in retryPolicy.outboundHeaders()) {
      set(name, value.toMutableList())
    }

    return this
  }

  /**
   * Add an upstream HTTP protocol to use when executing this request.
   *
   * @param upstreamHttpProtocol: The protocol to use for this request.
   *
   * @return RequestHeadersBuilder, This builder.
   */
  fun addUpstreamHttpProtocol(upstreamHttpProtocol: UpstreamHttpProtocol)
   : RequestHeadersBuilder
  {
    set("x-envoy-mobile-upstream-protocol", mutableListOf(upstreamHttpProtocol.stringValue))
    return this
  }

  /**
   * Build the request headers using the current builder.
   *
   * @return RequestHeaders, New instance of request headers.
   */
  fun build(): RequestHeaders {
    return RequestHeaders(headers)
  }
}

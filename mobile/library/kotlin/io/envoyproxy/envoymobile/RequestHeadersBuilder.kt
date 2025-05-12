package io.envoyproxy.envoymobile

/** Builder used for constructing instances of RequestHeaders`. */
class RequestHeadersBuilder : HeadersBuilder {
  /**
   * Initialize a new instance of the builder.
   *
   * @param method: Method for the request.
   * @param scheme: The URL scheme for the request (i.e., "https").
   * @param authority: The URL authority for the request (i.e., "api.foo.com").
   * @param path: The URL path for the request (i.e., "/foo").
   */
  constructor(
    method: RequestMethod,
    scheme: String = "https",
    authority: String,
    path: String
  ) : super(
    HeadersContainer(
      mapOf(
        ":authority" to mutableListOf(authority),
        ":method" to mutableListOf(method.stringValue),
        ":path" to mutableListOf(path),
        ":scheme" to mutableListOf(scheme)
      )
    )
  )

  /**
   * Instantiate a new builder. Used only by RequestHeaders to convert back to
   * RequestHeadersBuilder.
   *
   * @param headers: The headers to start with.
   */
  constructor(headers: Map<String, MutableList<String>>) : super(HeadersContainer(headers))

  /**
   * Instantiate a new builder.
   *
   * @param container: The headers container to start with.
   */
  constructor(container: HeadersContainer) : super(container)

  override fun add(name: String, value: String): RequestHeadersBuilder {
    super.add(name, value)
    return this
  }

  override fun set(name: String, value: MutableList<String>): RequestHeadersBuilder {
    super.set(name, value)
    return this
  }

  override fun remove(name: String): RequestHeadersBuilder {
    super.remove(name)
    return this
  }

  override fun internalSet(name: String, value: MutableList<String>): RequestHeadersBuilder {
    super.internalSet(name, value)
    return this
  }

  /**
   * Add a retry policy to be used with this request.
   *
   * @param retryPolicy: The retry policy to use.
   * @return RequestHeadersBuilder, This builder.
   */
  fun addRetryPolicy(retryPolicy: RetryPolicy): RequestHeadersBuilder {
    for ((name, value) in retryPolicy.outboundHeaders()) {
      internalSet(name, value.toMutableList())
    }

    return this
  }

  /**
   * Add a socket tag to be applied to the socket.
   *
   * @param uid: Traffic stats UID to be applied.
   * @param tag: Traffic stats tag to be applied.
   *
   * See: https://source.android.com/devices/tech/datausage/tags-explained See:
   * https://developer.android.com/reference/android/net/TrafficStats#setThreadStatsTag(int) See:
   * https://developer.android.com/reference/android/net/TrafficStats#setThreadStatsUid(int) See:
   * https://developer.android.com/reference/android/net/TrafficStats#tagSocket(java.net.Socket)
   *
   * @return RequestHeadersBuilder, This builder.
   */
  fun addSocketTag(uid: Int, tag: Int): RequestHeadersBuilder {
    internalSet("x-envoy-mobile-socket-tag", mutableListOf(uid.toString() + "," + tag.toString()))
    return this
  }

  /**
   * Build the request headers using the current builder.
   *
   * @return RequestHeaders, New instance of request headers.
   */
  fun build(): RequestHeaders {
    return RequestHeaders(container)
  }
}

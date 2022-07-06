package io.envoyproxy.envoymobile

/**
 * Headers representing an outbound request.
 */
open class RequestHeaders : Headers {
  /**
   * Internal constructor used by builders.
   *
   * @param headers: Headers to set.
   */
  internal constructor(headers: Map<String, List<String>>) : super(HeadersContainer.create(headers))

  internal constructor(container: HeadersContainer): super(container)

  /**
   * Method for the request.
   */
  val method: RequestMethod by lazy { RequestMethod.enumValue(value(":method")?.first()!!) }

  /**
   * The URL scheme for the request (i.e., "https").
   */
  val scheme: String by lazy { value(":scheme")?.first()!! }

  /**
   * The URL authority for the request (i.e., "api.foo.com").
   */
  val authority: String by lazy { value(":authority")?.first()!! }

  /**
   * The URL path for the request (i.e., "/foo").
   */
  val path: String by lazy { value(":path")?.first()!! }

  /**
   * Retry policy to use for this request.
   */
  val retryPolicy: RetryPolicy? by lazy { RetryPolicy.from(this) }

  /**
   * The protocol version to use for upstream requests.
   */
  val upstreamHttpProtocol: UpstreamHttpProtocol? by lazy {
    value("x-envoy-mobile-upstream-protocol")?.firstOrNull()
      ?.let { UpstreamHttpProtocol.enumValue(it) }
  }

  /**
   * Convert the headers back to a builder for mutation.
   *
   * @return RequestHeadersBuilder, The new builder.
   */
  fun toRequestHeadersBuilder() = RequestHeadersBuilder(container)
}

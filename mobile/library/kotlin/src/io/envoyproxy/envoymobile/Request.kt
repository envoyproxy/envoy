package io.envoyproxy.envoymobile

/**
 * Represents an Envoy HTTP request. Use `RequestBuilder` to construct new instances.
 *
 * @param method Method for the request.
 * @param scheme The URL scheme for the request (i.e., "https").
 * @param authority The URL authority for the request (i.e., "api.foo.com").
 * @param path The URL path for the request (i.e., "/foo").
 */
data class Request internal constructor(
    val method: RequestMethod,
    val scheme: String,
    val authority: String,
    val path: String,
    val headers: Map<String, List<String>>,
    val retryPolicy: RetryPolicy?
) {

  /**
   * Transforms this Request to the {@link io.envoyproxy.envoymobile.RequestBuilder} for modification using the
   * current properties.
   *
   * @return the builder.
   */
  fun toBuilder(): RequestBuilder {
    return RequestBuilder(method, scheme, authority, path)
        .setHeaders(headers)
        .addRetryPolicy(retryPolicy)
  }
}

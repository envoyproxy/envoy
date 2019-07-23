package io.envoyproxy.envoymobile

/**
 * Represents an Envoy HTTP request. Use `RequestBuilder` to construct new instances.
 *
 * @param method Method for the request.
 * @param scheme The URL scheme for the request (i.e., "https").
 * @param authority The URL authority for the request (i.e., "api.foo.com").
 * @param path The URL path for the request (i.e., "/foo").
 */
class Request internal constructor(
    val method: RequestMethod,
    val scheme: String,
    val authority: String,
    val path: String,
    val headers: Map<String, List<String>>,
    val trailers: Map<String, List<String>>,
    val body: ByteArray?,
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
        .setTrailers(trailers)
        .addBody(body)
        .addRetryPolicy(retryPolicy)
  }

  override fun equals(other: Any?): Boolean {
    if (this === other) return true
    if (javaClass != other?.javaClass) return false

    other as Request

    if (method != other.method) return false
    if (scheme != other.scheme) return false
    if (authority != other.authority) return false
    if (path != other.path) return false
    if (headers != other.headers) return false
    if (trailers != other.trailers) return false
    if (body != null) {
      if (other.body == null) return false
      if (!body.contentEquals(other.body)) return false
    } else if (other.body != null) return false
    if (retryPolicy != other.retryPolicy) return false

    return true
  }

  override fun hashCode(): Int {
    var result = method.hashCode()
    result = 31 * result + scheme.hashCode()
    result = 31 * result + authority.hashCode()
    result = 31 * result + path.hashCode()
    result = 31 * result + headers.hashCode()
    result = 31 * result + trailers.hashCode()
    result = 31 * result + (body?.contentHashCode() ?: 0)
    result = 31 * result + (retryPolicy?.hashCode() ?: 0)
    return result
  }
}

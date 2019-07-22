package io.envoyproxy.envoymobile

import java.net.URL

class Request internal constructor(
    val method: RequestMethod,
    val url: URL,
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
    return RequestBuilder(url, method)
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
    if (url != other.url) return false
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
    result = 31 * result + url.hashCode()
    result = 31 * result + headers.hashCode()
    result = 31 * result + trailers.hashCode()
    result = 31 * result + (body?.contentHashCode() ?: 0)
    result = 31 * result + (retryPolicy?.hashCode() ?: 0)
    return result
  }
}

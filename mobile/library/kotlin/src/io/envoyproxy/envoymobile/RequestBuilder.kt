package io.envoyproxy.envoymobile

import java.net.URL


/**
 * Builder used for constructing instances of `Request` types.
 *
 * @param url URL for the request.
 * @param method Method for the request.
 */
class RequestBuilder(
    val url: URL,
    val method: RequestMethod
) {
  // Headers to send with the request.
  // Multiple values for a given name are valid, and will be sent as comma-separated values.
  private val headers: MutableMap<String, MutableList<String>> = mutableMapOf()

  // Trailers to send with the request.
  // Multiple values for a given name are valid, and will be sent as comma-separated values.
  private val trailers: MutableMap<String, MutableList<String>> = mutableMapOf()

  // Serialized data to send as the body of the request.
  private var body: ByteArray? = null

  // Retry policy to use for this request.
  private var retryPolicy: RetryPolicy? = null

  /**
   * Serialized data to send as the body of the request.
   */
  fun addBody(body: ByteArray?): RequestBuilder {
    this.body = body
    return this
  }

  /**
   * Add a retry policy to use for this request.
   *
   * @param retryPolicy the {@link io.envoyproxy.envoymobile.RetryPolicy} for this request.
   * @return this builder.
   */
  fun addRetryPolicy(retryPolicy: RetryPolicy?): RequestBuilder {
    this.retryPolicy = retryPolicy
    return this
  }

  /**
   * Append a value to the header key.
   *
   * @param name the header key.
   * @param value the value associated to the header key.
   * @return this builder.
   */
  fun addHeader(name: String, value: String): RequestBuilder {
    if (headers.containsKey(name)) {
      headers[name]!!.add(value)
    } else {
      headers[name] = mutableListOf(value)
    }
    return this
  }

  /**
   * Remove the value in the specified header.
   *
   * @param name the header key to remove.
   * @param value the value to be removed.
   * @return this builder.
   */
  fun removeHeader(name: String, value: String): RequestBuilder {
    if (headers.containsKey(name)) {
      headers[name]!!.remove(value)
      if (headers[name]!!.isEmpty()) {
        headers.remove(name)
      }
    }
    return this
  }

  /**
   * Remove all headers with this name.
   *
   * @param name the header key to remove.
   * @return this builder.
   */
  fun removeHeaders(name: String): RequestBuilder {
    headers.remove(name)
    return this
  }

  /**
   * Append a value to the trailer key.
   *
   * @param name the trailer key.
   * @param value the value associated to the trailer key.
   * @return this builder.
   */
  fun addTrailer(name: String, value: String): RequestBuilder {
    if (trailers.containsKey(name)) {
      trailers[name]!!.add(value)
    } else {
      trailers[name] = mutableListOf(value)
    }
    return this
  }

  /**
   * Remove the value in the specified trailer.
   *
   * @param name the trailer key to remove.
   * @param value the value to be removed.
   * @return this builder.
   */
  fun removeTrailers(name: String): RequestBuilder {
    trailers.remove(name)
    return this
  }

  /**
   * Remove the value in the specified trailer.
   *
   * @param name the trailer key to remove.
   * @param value the value to be removed.
   * @return this builder.
   */
  fun removeTrailer(name: String, value: String): RequestBuilder {
    if (trailers.containsKey(name)) {
      trailers[name]!!.remove(value)

      if (trailers[name]!!.isEmpty()) {
        trailers.remove(name)
      }
    }
    return this
  }

  /**
   * Creates the {@link io.envoyproxy.envoymobile.Request} object using the data set in the builder
   *
   * @return the {@link io.envoyproxy.envoymobile.Request} object
   */
  fun build(): Request {
    return Request(
        method,
        url,
        headers,
        trailers,
        body,
        retryPolicy
    )
  }

  internal fun setHeaders(headers: Map<String, List<String>>): RequestBuilder {
    this.headers.clear()
    for (entry in headers) {
      this.headers[entry.key] = entry.value.toMutableList()
    }
    return this
  }

  internal fun setTrailers(trailers: Map<String, List<String>>): RequestBuilder {
    this.trailers.clear()
    for (entry in trailers) {
      this.trailers[entry.key] = entry.value.toMutableList()
    }
    return this
  }
}

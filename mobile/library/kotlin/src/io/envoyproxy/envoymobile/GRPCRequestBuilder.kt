package io.envoyproxy.envoymobile

/**
 * Builder used for creating new gRPC `Request` instances.
 */
class GRPCRequestBuilder(
    path: String,
    authority: String,
    useHTTPS: Boolean
) {
  private val underlyingBuilder: RequestBuilder = RequestBuilder(
      method = RequestMethod.POST,
      scheme = if (useHTTPS) "https" else "http",
      authority = authority,
      path = path)

  init {
    underlyingBuilder.addHeader("content-type", "application/grpc")
  }

  /**
   * Append a value to the header key.
   *
   * @param name the header key.
   * @param value the value associated to the header key.
   * @return this builder.
   */
  fun addHeader(name: String, value: String): GRPCRequestBuilder {
    underlyingBuilder.addHeader(name, value)
    return this
  }

  /**
   * Remove the value in the specified header.
   *
   * @param name the header key to remove.
   * @param value the value to be removed.
   * @return this builder.
   */
  fun removeHeader(name: String, value: String): GRPCRequestBuilder {
    underlyingBuilder.removeHeader(name, value)
    return this
  }

  /**
   * Remove all headers with this name.
   *
   * @param name the header key to remove.
   * @return this builder.
   */
  fun removeHeaders(name: String): GRPCRequestBuilder {
    underlyingBuilder.removeHeaders(name)
    return this
  }

  /**
   * Add a specific timeout for the gRPC request. This will be sent in the `grpc-timeout` header.
   *
   * @param timeoutMS Timeout, in milliseconds.
   * @return this builder.
   */
  fun addTimeoutMS(timeoutMS: Int?): GRPCRequestBuilder {
    val headerName = "grpc-timeout"
    if (timeoutMS == null) {
      removeHeaders(headerName)
    } else {
      addHeader(headerName, "${timeoutMS}m")
    }
    return this
  }

  /**
   * Creates the Request object using the data set in the builder.
   *
   * @return the Request object.
   */
  fun build(): Request {
    return underlyingBuilder.build()
  }
}

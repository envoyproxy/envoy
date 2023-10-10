package io.envoyproxy.envoymobile

/** Utility to enable request compression. */
object RequestHeadersBuilderCompressionUtil {
  /**
   * Compress this request's body using the specified algorithm. Will only apply if the content
   * length exceeds 30 bytes.
   *
   * @param algorithm: The compression algorithm to use to compress this request.
   * @return RequestHeadersBuilder, This builder.
   */
  fun RequestHeadersBuilder.enableRequestCompression(
    algorithm: CompressionAlgorithm
  ): RequestHeadersBuilder {
    internalSet("x-envoy-mobile-compression", mutableListOf(algorithm.stringValue))
    return this
  }
}

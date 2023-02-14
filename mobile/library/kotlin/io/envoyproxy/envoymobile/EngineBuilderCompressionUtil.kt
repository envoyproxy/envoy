package io.envoyproxy.envoymobile

/**
 * Utility to enable request compression.
 */
object EngineBuilderCompressionUtil {
  /**
   * Specify whether to do gzip request compression or not.  Defaults to false.
   *
   * @param enableGzipCompression whether or not to gunzip requests.
   *
   * @return This builder.
   */
  fun EngineBuilder.enableGzipCompression(enableGzipCompression: Boolean): EngineBuilder {
    this.enableGzipCompression = enableGzipCompression
    return this
  }

  /**
   * Specify whether to do brotli request compression or not.  Defaults to false.
   *
   * @param enableBrotliCompression whether or not to brotli compress requests.
   *
   * @return This builder.
   */
  fun EngineBuilder.enableBrotliCompression(enableBrotliCompression: Boolean): EngineBuilder {
    this.enableBrotliCompression = enableBrotliCompression
    return this
  }
}

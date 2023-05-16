package io.envoyproxy.envoymobile

/**
 * Available algorithms to compress requests.
 *
 * @param stringValue string representation of a given compression algorithm.
 */
enum class CompressionAlgorithm(internal val stringValue: String) {
  GZIP("gzip"),
  BROTLI("brotli"),
}

package io.envoyproxy.envoymobile

enum class CompressionAlgorithm(internal val stringValue: String) {
  GZIP("gzip"),
  BROTLI("brotli"),
}

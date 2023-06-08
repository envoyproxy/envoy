#if ENVOY_MOBILE_REQUEST_COMPRESSION
/// Available algorithms to compress requests.
public enum CompressionAlgorithm: String {
  case gzip
  case brotli
}
#endif

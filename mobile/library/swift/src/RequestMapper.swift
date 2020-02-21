private let kRestrictedHeaderPrefixes = [":", "x-envoy-mobile"]

extension Request {
  /// Returns a set of outbound headers that include HTTP
  /// information on the URL, method, and additional headers.
  ///
  /// - returns: Outbound headers to send with an HTTP request.
  func outboundHeaders() -> [String: [String]] {
    var headers = self.headers
      .filter { !kRestrictedHeaderPrefixes.contains(where: $0.key.hasPrefix) }
      .reduce(into: [
        ":method": [self.method.stringValue],
        ":scheme": [self.scheme],
        ":authority": [self.authority],
        ":path": [self.path],
      ]) { $0[$1.key] = $1.value }

    if let retryPolicy = self.retryPolicy {
      headers = headers.merging(retryPolicy.outboundHeaders()) { _, retryHeader in retryHeader }
    }

    if let upstreamHttpProtocol = self.upstreamHttpProtocol {
      headers["x-envoy-mobile-upstream-protocol"] = [upstreamHttpProtocol.stringValue]
    }

    return headers
  }
}

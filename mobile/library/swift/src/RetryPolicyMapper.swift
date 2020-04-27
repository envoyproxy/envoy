extension RetryPolicy {
  /// Converts the retry policy to a set of headers recognized by Envoy.
  ///
  /// - returns: The header representation of the retry policy.
  func outboundHeaders() -> [String: [String]] {
    var headers = [
      "x-envoy-max-retries": ["\(self.maxRetryCount)"],
      "x-envoy-upstream-rq-timeout-ms": ["\(self.totalUpstreamTimeoutMS ?? 0)"],
    ]

    var retryOn = self.retryOn.map { $0.stringValue }
    if !self.retryStatusCodes.isEmpty {
      retryOn.append("retriable-status-codes")
      headers["x-envoy-retriable-status-codes"] = self.retryStatusCodes.map { "\($0)" }
    }

    headers["x-envoy-retry-on"] = retryOn

    if let perRetryTimeoutMS = self.perRetryTimeoutMS {
      headers["x-envoy-upstream-rq-per-try-timeout-ms"] = ["\(perRetryTimeoutMS)"]
    }

    return headers
  }
}

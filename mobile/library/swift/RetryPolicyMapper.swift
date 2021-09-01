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

  // Envoy internally coalesces multiple x-envoy header values into one comma-delimited value.
  // These functions split those values up to correctly map back to Swift enums.
  static func splitRetryRule(value: String) -> [RetryRule] {
    return value.components(separatedBy: ",").compactMap(RetryRule.init)
  }
  static func splitRetriableStatusCodes(value: String) -> [UInt] {
    return value.components(separatedBy: ",").compactMap(UInt.init)
  }

  /// Initialize the retry policy from a set of headers.
  ///
  /// - parameter headers: The headers with which to initialize the retry policy.
  static func from(headers: Headers) -> RetryPolicy? {
    guard let maxRetryCount = headers.value(forName: "x-envoy-max-retries")?
      .first.flatMap(UInt.init) else
    {
      return nil
    }

    return RetryPolicy(
      maxRetryCount: maxRetryCount,
      retryOn: headers.value(forName: "x-envoy-retry-on")?
        .flatMap(RetryPolicy.splitRetryRule) ?? [],
      retryStatusCodes: headers.value(forName: "x-envoy-retriable-status-codes")?
        .flatMap(RetryPolicy.splitRetriableStatusCodes) ?? [],
      perRetryTimeoutMS: headers.value(forName: "x-envoy-upstream-rq-per-try-timeout-ms")?
        .first.flatMap(UInt.init),
      totalUpstreamTimeoutMS: headers.value(forName: "x-envoy-upstream-rq-timeout-ms")?
        .first.flatMap(UInt.init)
    )
  }
}

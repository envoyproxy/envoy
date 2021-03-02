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
      retryOn: headers.value(forName: "x-envoy-retry-on")?.compactMap(RetryRule.init) ?? [],
      retryStatusCodes: headers.value(forName: "x-envoy-retriable-status-codes")?
        .compactMap(UInt.init) ?? [],
      perRetryTimeoutMS: headers.value(forName: "x-envoy-upstream-rq-per-try-timeout-ms")?
        .first.flatMap(UInt.init),
      totalUpstreamTimeoutMS: headers.value(forName: "x-envoy-upstream-rq-timeout-ms")?
        .first.flatMap(UInt.init)
    )
  }
}

import Foundation

/// Rules that may be used with `RetryPolicy`.
/// See the `x-envoy-retry-on` Envoy header for documentation.
@objc
public enum RetryRule: Int, CaseIterable {
  case status5xx
  case gatewayError
  case connectFailure
  case refusedStream
  case retriable4xx
  case retriableStatusCodes
  case retriableHeaders
  case reset

  /// String representation of this rule.
  var stringValue: String {
    switch self {
    case .status5xx:
      return "5xx"
    case .gatewayError:
      return "gateway-error"
    case .connectFailure:
      return "connect-failure"
    case .refusedStream:
      return "refused-stream"
    case .retriable4xx:
      return "retriable-4xx"
    case .retriableStatusCodes:
      return "retriable-status-codes"
    case .retriableHeaders:
      return "retriable-headers"
    case .reset:
      return "reset"
    }
  }
}

/// Specifies how a request may be retried, containing one or more rules.
/// https://www.envoyproxy.io/learn/automatic-retries
@objcMembers
public final class RetryPolicy: NSObject {
  public let maxRetryCount: UInt
  public let retryOn: [RetryRule]
  public let perRetryTimeoutMS: UInt?
  public let totalUpstreamTimeoutMS: UInt?

  /// Designated initializer.
  ///
  /// - parameter maxRetryCount:          Maximum number of retries that a request may be
  ///                                     performed.
  /// - parameter retryOn:                Whitelist of rules used for retrying.
  /// - parameter perRetryTimeoutMS:      Timeout (in milliseconds) to apply to each retry. Must
  ///                                     be <= `totalUpstreamTimeoutMS` if it's a positive number.
  /// - parameter totalUpstreamTimeoutMS: Total timeout (in milliseconds) that includes all
  ///                                     retries. Spans the point at which the entire downstream
  ///                                     request has been processed and when the upstream
  ///                                     response has been completely processed.
  ///                                     Nil or 0 may be specified to disable it.
  public init(maxRetryCount: UInt, retryOn: [RetryRule], perRetryTimeoutMS: UInt? = nil,
              totalUpstreamTimeoutMS: UInt? = 15_000)
  {
    if let perRetryTimeoutMS = perRetryTimeoutMS,
      let totalUpstreamTimeoutMS = totalUpstreamTimeoutMS
    {
      assert(perRetryTimeoutMS <= totalUpstreamTimeoutMS || totalUpstreamTimeoutMS == 0,
             "Per-retry timeout cannot be less than total timeout")
    }

    self.maxRetryCount = maxRetryCount
    self.retryOn = retryOn
    self.perRetryTimeoutMS = perRetryTimeoutMS
    self.totalUpstreamTimeoutMS = totalUpstreamTimeoutMS
  }
}

// MARK: - Equatable overrides

extension RetryPolicy {
  public override func isEqual(_ object: Any?) -> Bool {
    guard let other = object as? RetryPolicy else {
      return false
    }

    return self.maxRetryCount == other.maxRetryCount
      && self.retryOn == other.retryOn
      && self.perRetryTimeoutMS == other.perRetryTimeoutMS
      && self.totalUpstreamTimeoutMS == other.totalUpstreamTimeoutMS
  }
}

import Foundation

/// Rules that may be used with `RetryPolicy`.
/// See the `x-envoy-retry-on` Envoy header for documentation.
@objc
public enum RetryRule: Int {
  case fiveXX
  case gatewayError
  case connectFailure
  case retriableFourXX
  case refusedUpstream

  /// String representation of this rule.
  var stringValue: String {
    switch self {
    case .fiveXX:
      return "5xx"
    case .gatewayError:
      return "gateway-error"
    case .connectFailure:
      return "connect-failure"
    case .retriableFourXX:
      return "retriable-4xx"
    case .refusedUpstream:
      return "refused-upstream"
    }
  }
}

/// Specifies how a request may be retried, containing one or more rules.
/// https://www.envoyproxy.io/learn/automatic-retries
@objcMembers
public final class RetryPolicy: NSObject {
  /// Maximum number of retries that a request may be performed.
  public let maxRetryCount: UInt
  /// Whitelist of rules used for retrying.
  public let retryOn: [RetryRule]
  /// Timeout (in milliseconds) to apply to each retry.
  public let perRetryTimeoutMS: UInt?

  /// Public initializer.
  public init(maxRetryCount: UInt, retryOn: [RetryRule], perRetryTimeoutMS: UInt?) {
    self.maxRetryCount = maxRetryCount
    self.retryOn = retryOn
    self.perRetryTimeoutMS = perRetryTimeoutMS
  }
}

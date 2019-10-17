@testable import Envoy
import XCTest

final class RetryPolicyMapperTests: XCTestCase {
  func testConvertingToHeadersWithPerRetryTimeoutIncludesAllHeaders() {
    let policy = RetryPolicy(maxRetryCount: 3,
                             retryOn: RetryRule.allCases,
                             perRetryTimeoutMS: 15_000,
                             totalUpstreamTimeoutMS: 60_000)
    let expectedHeaders = [
      "x-envoy-max-retries": ["3"],
      "x-envoy-retry-on": [
        "5xx", "gateway-error", "connect-failure", "retriable-4xx", "refused-upstream",
      ],
      "x-envoy-upstream-rq-per-try-timeout-ms": ["15000"],
      "x-envoy-upstream-rq-timeout-ms": ["60000"],
    ]

    XCTAssertEqual(expectedHeaders, policy.outboundHeaders())
  }

  func testConvertingToHeadersWithoutRetryTimeoutExcludesPerRetryTimeoutHeader() {
    let policy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases)
    XCTAssertNil(policy.outboundHeaders()["x-envoy-upstream-rq-per-try-timeout-ms"])
  }
}

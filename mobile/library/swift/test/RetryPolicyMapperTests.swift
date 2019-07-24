@testable import Envoy
import XCTest

final class RetryPolicyMapperTests: XCTestCase {
  func testConvertingToHeadersWithPerRetryTimeoutIncludesAllHeaders() {
    let policy = RetryPolicy(maxRetryCount: 123,
                             retryOn: RetryRule.allCases,
                             perRetryTimeoutMS: 9001)
    let expectedHeaders = [
      "x-envoy-max-retries": "123",
      "x-envoy-retry-on": "5xx,gateway-error,connect-failure,retriable-4xx,refused-upstream",
      "x-envoy-upstream-rq-per-try-timeout-ms": "9001",
    ]

    XCTAssertEqual(expectedHeaders, policy.outboundHeaders())
  }

  func testConvertingToHeadersWithoutRetryTimeoutExcludesPerRetryTimeoutHeader() {
    let policy = RetryPolicy(maxRetryCount: 123,
                             retryOn: RetryRule.allCases)
    let expectedHeaders = [
      "x-envoy-max-retries": "123",
      "x-envoy-retry-on": "5xx,gateway-error,connect-failure,retriable-4xx,refused-upstream",
    ]

    XCTAssertEqual(expectedHeaders, policy.outboundHeaders())
  }
}

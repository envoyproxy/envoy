@testable import Envoy
import XCTest

final class RetryPolicyMapperTests: XCTestCase {
  func testConvertingToHeadersWithPerRetryTimeoutIncludesAllHeaders() {
    let policy = RetryPolicy(maxRetryCount: 3,
                             retryOn: RetryRule.allCases,
                             retryStatusCodes: [400, 422, 500],
                             perRetryTimeoutMS: 15_000,
                             totalUpstreamTimeoutMS: 60_000)
    let expectedHeaders = [
      "x-envoy-retriable-status-codes": ["400", "422", "500"],
      "x-envoy-max-retries": ["3"],
      "x-envoy-retry-on": [
        "5xx", "gateway-error", "connect-failure", "refused-stream", "retriable-4xx",
        "retriable-headers", "reset", "retriable-status-codes",
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

  func testConvertingToHeadersWithoutUpstreamTimeoutIncludesZeroForTimeoutHeader() {
    let policy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                             totalUpstreamTimeoutMS: nil)
    XCTAssertEqual(["0"], policy.outboundHeaders()["x-envoy-upstream-rq-timeout-ms"])
  }

  func testConvertingToHeadersWithoutRetryStatusCodesDoesNotSetRetriableStatusCodeHeaders() throws {
    let policy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                             retryStatusCodes: [])
    let headers = policy.outboundHeaders()
    XCTAssertNil(headers["x-envoy-retriable-status-codes"])
    XCTAssertFalse((try XCTUnwrap(headers["x-envoy-retry-on"])).contains("retriable-status-codes"))
  }
}

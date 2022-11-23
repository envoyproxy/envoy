import Envoy
import XCTest

final class RetryPolicyTests: XCTestCase {
  func testRetryPoliciesAreEqualWhenPropertiesAreEqual() {
    let policy1 = RetryPolicy(maxRetryCount: 123,
                              retryOn: [.connectFailure],
                              perRetryTimeoutMS: 8000)
    let policy2 = RetryPolicy(maxRetryCount: 123,
                              retryOn: [.connectFailure],
                              perRetryTimeoutMS: 8000)
    XCTAssertEqual(policy1, policy2)
  }

  func testPointersAreNotEqualWhenInstancesAreDifferent() {
    let policy1 = RetryPolicy(maxRetryCount: 123,
                              retryOn: [.connectFailure],
                              perRetryTimeoutMS: 8000)
    let policy2 = RetryPolicy(maxRetryCount: 123,
                              retryOn: [.connectFailure],
                              perRetryTimeoutMS: 8000)
    XCTAssert(policy1 !== policy2)
  }
}

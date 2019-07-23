@testable import Envoy
import XCTest

final class RequestMapperTests: XCTestCase {
  func testAddsMethodToHeaders() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .build()
      .outboundHeaders()
    XCTAssertEqual("POST", headers[":method"])
  }

  func testAddsSchemeToHeaders() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .build()
      .outboundHeaders()
    XCTAssertEqual("https", headers[":scheme"])
  }

  func testAddsAuthorityToHeaders() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .build()
      .outboundHeaders()
    XCTAssertEqual("x.y.z", headers[":authority"])
  }

  func testAddsPathToHeaders() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .build()
      .outboundHeaders()
    XCTAssertEqual("/foo", headers[":path"])
  }

  func testJoinsHeaderValuesWithTheSameKey() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .addHeader(name: "foo", value: "1")
      .addHeader(name: "foo", value: "2")
      .build()
      .outboundHeaders()
    XCTAssertEqual("1,2", headers["foo"])
  }

  func testStripsHeadersWithSemicolonPrefix() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .addHeader(name: ":restricted", value: "someValue")
      .build()
      .outboundHeaders()
    XCTAssertNil(headers[":restricted"])
  }

  func testCannotOverrideStandardRestrictedHeaders() {
    let headers = RequestBuilder(method: .post, scheme: "https", authority: "x.y.z", path: "/foo")
      .addHeader(name: ":scheme", value: "override")
      .addHeader(name: ":authority", value: "override")
      .addHeader(name: ":path", value: "override")
      .build()
      .outboundHeaders()

    XCTAssertEqual("https", headers[":scheme"])
    XCTAssertEqual("x.y.z", headers[":authority"])
    XCTAssertEqual("/foo", headers[":path"])
  }

  func testIncludesRetryPolicyHeaders() {
    let retryPolicy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                                  perRetryTimeoutMS: 9001)
    let retryHeaders = retryPolicy.outboundHeaders()
    let requestHeaders = RequestBuilder(method: .post, scheme: "https",
                                        authority: "x.y.z", path: "/foo")
      .addHeader(name: "foo", value: "bar")
      .addRetryPolicy(retryPolicy)
      .build()
      .outboundHeaders()

    XCTAssertEqual("bar", requestHeaders["foo"])
    XCTAssertFalse(retryHeaders.isEmpty)
    for (retryHeader, expectedValue) in retryHeaders {
      XCTAssertEqual(expectedValue, requestHeaders[retryHeader])
    }
  }

  func testRetryPolicyTakesPrecedenceOverManuallySetRetryHeaders() {
    let retryPolicy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                                  perRetryTimeoutMS: 9001)
    let requestHeaders = RequestBuilder(method: .post, scheme: "https",
                                        authority: "x.y.z", path: "/foo")
      .addHeader(name: "x-envoy-max-retries", value: "override")
      .addRetryPolicy(retryPolicy)
      .build()
      .outboundHeaders()

    XCTAssertEqual("123", requestHeaders["x-envoy-max-retries"])
  }
}

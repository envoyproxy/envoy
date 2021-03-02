@testable import Envoy
import XCTest

final class RequestHeadersBuilderTests: XCTestCase {
  func testAddsMethodToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .build()
    XCTAssertEqual(["POST"], headers.value(forName: ":method"))
    XCTAssertEqual(.post, headers.method)
  }

  func testAddsSchemeToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .build()
    XCTAssertEqual(["https"], headers.value(forName: ":scheme"))
    XCTAssertEqual("https", headers.scheme)
  }

  func testAddsAuthorityToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .build()
    XCTAssertEqual(["envoyproxy.io"], headers.value(forName: ":authority"))
    XCTAssertEqual("envoyproxy.io", headers.authority)
  }

  func testAddsPathToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .build()
    XCTAssertEqual(["/mock"], headers.value(forName: ":path"))
    XCTAssertEqual("/mock", headers.path)
  }

  func testAddsH1ToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .addUpstreamHttpProtocol(.http1)
        .build()
    XCTAssertEqual(["http1"], headers.value(forName: "x-envoy-mobile-upstream-protocol"))
    XCTAssertEqual(.http1, headers.upstreamHttpProtocol)
  }

  func testAddsH2ToHeaders() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .addUpstreamHttpProtocol(.http2)
        .build()
    XCTAssertEqual(["http2"], headers.value(forName: "x-envoy-mobile-upstream-protocol"))
    XCTAssertEqual(.http2, headers.upstreamHttpProtocol)
  }

  func testJoinsHeaderValuesWithTheSameKey() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
        .add(name: "x-foo", value: "1")
        .add(name: "x-foo", value: "2")
        .build()
    XCTAssertEqual(["1", "2"], headers.value(forName: "x-foo"))
  }

  func testCannotPubliclyAddHeadersWithRestrictedPrefix() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .set(name: ":x-foo", value: ["123"])
      .set(name: "x-envoy-mobile-foo", value: ["abc"])
      .build()
    XCTAssertNil(headers.value(forName: ":x-foo"))
    XCTAssertNil(headers.value(forName: "x-envoy-mobile-foo"))
  }

  func testCannotPubliclySetHeadersWithRestrictedPrefix() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .add(name: ":x-foo", value: "123")
      .add(name: "x-envoy-mobile-foo", value: "abc")
      .build()
    XCTAssertNil(headers.value(forName: ":x-foo"))
    XCTAssertNil(headers.value(forName: "x-envoy-mobile-foo"))
  }

  func testCannotPubliclyRemoveHeadersWithRestrictedPrefix() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .addUpstreamHttpProtocol(.http2)
      .remove(name: ":path")
      .remove(name: "x-envoy-mobile-upstream-protocol")
      .build()
    XCTAssertEqual(["/mock"], headers.value(forName: ":path"))
    XCTAssertEqual(["http2"], headers.value(forName: "x-envoy-mobile-upstream-protocol"))
  }

  func testCanInternallySetHeadersWithRestrictedPrefix() {
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .internalSet(name: ":x-foo", value: ["123"])
      .internalSet(name: "x-envoy-mobile-foo", value: ["abc"])
      .build()
    XCTAssertEqual(["123"], headers.value(forName: ":x-foo"))
    XCTAssertEqual(["abc"], headers.value(forName: "x-envoy-mobile-foo"))
  }

  func testIncludesRetryPolicyHeaders() {
    let retryPolicy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                                  perRetryTimeoutMS: 9001)
    let retryHeaders = retryPolicy.outboundHeaders()
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .addRetryPolicy(retryPolicy)
      .build()

    XCTAssertFalse(retryHeaders.isEmpty)
    XCTAssertEqual(retryPolicy, headers.retryPolicy)
    for (retryHeader, expectedValue) in retryHeaders {
      XCTAssertEqual(expectedValue, headers.value(forName: retryHeader))
    }
  }

  func testRetryPolicyTakesPrecedenceOverManuallySetRetryHeaders() {
    let retryPolicy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                                  perRetryTimeoutMS: 9001)
    let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                        authority: "envoyproxy.io", path: "/mock")
      .add(name: "x-envoy-max-retries", value: "override")
      .addRetryPolicy(retryPolicy)
      .build()

    XCTAssertEqual(["123"], headers.value(forName: "x-envoy-max-retries"))
  }

  func testConvertingToRequestHeadersAndBackMaintainsEquality() {
    let headers1 = RequestHeadersBuilder(method: .post, scheme: "https",
                                         authority: "envoyproxy.io", path: "/mock")
      .build()
    let headers2 = headers1.toRequestHeadersBuilder().build()
    XCTAssertEqual(headers1, headers2)
  }

  func testConvertingRetryPolicyToHeadersAndBackCreatesTheSameRetryPolicy() {
    let retryPolicy = RetryPolicy(maxRetryCount: 123, retryOn: RetryRule.allCases,
                                  retryStatusCodes: [400, 410], perRetryTimeoutMS: 9001)
    let headers = Headers(headers: retryPolicy.outboundHeaders())
    XCTAssertEqual(retryPolicy, RetryPolicy.from(headers: headers))
  }

  func testConvertingRequestMethodToStringAndBackCreatesTheSameRequestMethod() {
    for method in RequestMethod.allCases {
      XCTAssertEqual(method, RequestMethod(stringValue: method.stringValue))
    }
  }

  func testConvertingHttpProtocolToStringAndBackCreatesTheSameHttpProtocol() {
    for httpProtocol in UpstreamHttpProtocol.allCases {
      XCTAssertEqual(httpProtocol, UpstreamHttpProtocol(stringValue: httpProtocol.stringValue))
    }
  }
}

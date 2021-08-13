import Envoy
import XCTest

private final class MockHeaderMutationFilter: RequestFilter {
  private let headersToAdd: [String: String]

  init(headersToAdd: [String: String]) {
    self.headersToAdd = headersToAdd
  }

  func onRequestHeaders(_ headers: RequestHeaders, endStream: Bool, streamIntel: StreamIntel)
    -> FilterHeadersStatus<RequestHeaders>
  {
    let builder = headers.toRequestHeadersBuilder()
    for (name, value) in self.headersToAdd {
      builder.add(name: name, value: value)
    }
    return .continue(headers: builder.build())
  }

  func onRequestData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
    -> FilterDataStatus<RequestHeaders>
  {
    return .continue(data: body)
  }

  func onRequestTrailers(_ trailers: RequestTrailers, streamIntel: StreamIntel)
    -> FilterTrailersStatus<RequestHeaders, RequestTrailers>
  {
    return .continue(trailers: trailers)
  }
}

final class DirectResponseFilterMutationIntegrationTest: XCTestCase {
  func testDirectResponseThatOnlyMatchesWhenUsingHeadersAddedByFilter() {
    let headersExpectation = self.expectation(description: "Response headers received")
    let dataExpectation = self.expectation(description: "Response data received")

    let requestHeaders = RequestHeadersBuilder(
      method: .get, authority: "127.0.0.1", path: "/v1/abc"
    )
    .build()

    // This test validates that Envoy is able to properly route direct responses when a filter
    // mutates the outbound request in a way that makes it match one of the direct response
    // configurations (whereas if the filter was not present in the chain, the request would not
    // match any configurations). This behavior is provided by the C++ `RouteCacheResetFilter`.
    let engine = TestEngineBuilder()
      .addPlatformFilter { MockHeaderMutationFilter(headersToAdd: ["x-foo": "123"]) }
      .addDirectResponse(
        .init(
          matcher: RouteMatcher(
            fullPath: "/v1/abc", headers: [
              .init(name: "x-foo", value: "123", mode: .exact),
            ]
          ),
          status: 200, body: "hello world", headers: ["x-response-foo": "aaa"]
        )
      )
      .build()

    var responseBuffer = Data()
    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { headers, endStream, _ in
        XCTAssertEqual(200, headers.httpStatus)
        XCTAssertEqual(["aaa"], headers.value(forName: "x-response-foo"))
        XCTAssertFalse(endStream)
        headersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream, _ in
        responseBuffer.append(contentsOf: data)
        if endStream {
          XCTAssertEqual("hello world", String(data: responseBuffer, encoding: .utf8))
          dataExpectation.fulfill()
        }
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    let expectations = [headersExpectation, dataExpectation]
    XCTAssertEqual(.completed, XCTWaiter().wait(for: expectations, timeout: 10, enforceOrder: true))
  }
}

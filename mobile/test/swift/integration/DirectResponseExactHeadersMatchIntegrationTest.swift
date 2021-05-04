import Envoy
import XCTest

final class DirectResponseExactHeadersMatchIntegrationTest: XCTestCase {
  func testDirectResponseWithExactHeadersMatch() {
    let headersExpectation = self.expectation(description: "Response headers received")
    let dataExpectation = self.expectation(description: "Response data received")

    let requestHeaders = RequestHeadersBuilder(
      method: .get, authority: "127.0.0.1", path: "/v1/abc"
    )
    .add(name: "x-foo", value: "123")
    .build()

    let engine = TestEngineBuilder()
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
      .setOnResponseHeaders { headers, endStream in
        XCTAssertEqual(200, headers.httpStatus)
        XCTAssertEqual(["aaa"], headers.value(forName: "x-response-foo"))
        XCTAssertFalse(endStream)
        headersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream in
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

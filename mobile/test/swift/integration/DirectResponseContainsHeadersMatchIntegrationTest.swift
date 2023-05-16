import Envoy
import TestExtensions
import XCTest
import EnvoyTestServerInterface
import Foundation

final class DirectResponseContainsHeadersMatchIntegrationTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testDirectResponseWithContainsHeadersMatch() {
    TestServerInterface.startHttpTestServer()
    let headersExpectation = self.expectation(description: "Response headers received")
    let dataExpectation = self.expectation(description: "Response data received")
    let port = String(TestServerInterface.getServerPort())
    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "http", authority: "localhost"+":"+port, path: "/"
    )
    .add(name: "x-foo", value: "123")
    .add(name: "x-foo", value: "456")
    .build()

    let engine = TestEngineBuilder()
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

    engine.terminate()
    TestServerInterface.shutdownTestServer()
  }
}

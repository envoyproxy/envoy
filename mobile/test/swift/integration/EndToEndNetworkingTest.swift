import Envoy
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class EndToEndNetworkingTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  override static func tearDown() {
    super.tearDown()
    // Flush the stdout and stderror to show the print output.
    fflush(stdout)
    fflush(stderr)
  }

  func testNetworkRequestReturnsHeadersAndData() {
    EnvoyTestServer.startHttp1PlaintextServer()
    EnvoyTestServer.setHeadersAndData(
      "x-response-foo", header_value: "aaa", response_body: "hello world")
    let headersExpectation = self.expectation(description: "Response headers received")
    let dataExpectation = self.expectation(description: "Response data received")
    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "http", authority: "localhost:" + port, path: "/"
    )
    .build()

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
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
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

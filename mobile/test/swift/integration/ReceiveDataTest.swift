import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class ReceiveDataTests: XCTestCase {
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

  func testReceiveData() {
    let directResponseBody = "response_body"
    EnvoyTestServer.startHttp1PlaintextServer()
    EnvoyTestServer.setHeadersAndData(
      "x-response-foo", header_value: "aaa", response_body: directResponseBody)

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/simple.txt")
      .build()

    let headersExpectation = self.expectation(description: "Run called with expected headers")
    let dataExpectation = self.expectation(description: "Run called with expected data")
    var actualResponseBody: String = ""

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         headersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream, _ in
        actualResponseBody.append(String(data: data, encoding: .utf8) ?? "")
        if endStream {
          dataExpectation.fulfill()
        }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [headersExpectation, dataExpectation], timeout: 10,
                                  enforceOrder: true),
                   .completed)

    XCTAssertEqual(actualResponseBody, directResponseBody)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class SendDataTests: XCTestCase {
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

  func testSendData() throws {
    EnvoyTestServer.startHttp1PlaintextServer()
    EnvoyTestServer.setHeadersAndData("x-response-foo", header_value: "aaa", response_body: "data")

    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
    let requestStringMatch = "match_me"

    let expectation = self.expectation(description: "Run called with expected http status")
    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addNativeFilter(
        name: "test_logger",
        // swiftlint:disable:next line_length
        typedConfig: "[\(assertionFilterType)] { match_config { http_request_generic_body_match: { patterns: { string_match: '\(requestStringMatch)'}}}}"
      )
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/simple.txt")
      .build()
    let body = try XCTUnwrap(requestStringMatch.data(using: .utf8))

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
      }
      .setOnResponseData { _, endStream, _ in
        if endStream {
          expectation.fulfill()
        }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .close(data: body)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

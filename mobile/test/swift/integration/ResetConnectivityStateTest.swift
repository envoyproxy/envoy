import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class ResetConnectivityStateTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testResetConnectivityState() {
    EnvoyTestServer.startHttp1PlaintextServer()
    let port = String(EnvoyTestServer.getEnvoyPort())

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addNativeFilter(
        name: "envoy.filters.http.assertion",
        // swiftlint:disable:next line_length
        typedConfig: "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion\",match_config: {http_request_headers_match: {patterns: [{string_match: \"localhost\"}]}}}")
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/test")
      .build()

    let expectation1 =
      self.expectation(description: "Run called with expected http status first request")

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation1.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation1], timeout: 10), .completed)

    engine.resetConnectivityState()

    let expectation2 =
      self.expectation(description: "Run called with expected http status first request")

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation2.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation2], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }
}

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

  func testSendData() throws {
    let requestStringMatch = "match_me"
    let expectation = self.expectation(description: "Run called with expected http status")
    EnvoyTestServer.startHttp1PlaintextServer()
    let port = String(EnvoyTestServer.getEnvoyPort())

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addNativeFilter(
        name: "envoy.filters.http.buffer",
        // swiftlint:disable:next line_length
        typedConfig: "{\"@type\":\"type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer\",\"\"max_request_bytes\":65000}")
      .addNativeFilter(
        name: "envoy.filters.http.assertion",
        // swiftlint:disable:next line_length
        typedConfig: "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion\",match_config: {http_request_generic_body_match: {patterns: [{string_match: \"match_me\"}]}}}")
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/test")
      .build()
    let body = try XCTUnwrap(requestStringMatch.data(using: .utf8))

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .close(data: body)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }
}

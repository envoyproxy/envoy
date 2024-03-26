import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class SendTrailersTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testSendTrailers() throws {
    let matcherTrailerName = "test-trailer"
    let matcherTrailerValue = "test.code"

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
        typedConfig: "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion\",match_config: {http_request_trailers_match: {headers: [{name: 'test-trailer', exact_match: 'test.code'}]}}}")
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/test")
      .build()
    let body = try XCTUnwrap("match_me".data(using: .utf8))
    let requestTrailers = RequestTrailersBuilder()
      .add(name: matcherTrailerName, value: matcherTrailerValue)
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         expectation.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .sendData(body)
      .close(trailers: requestTrailers)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }
}

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

  override static func tearDown() {
    super.tearDown()
    // Flush the stdout and stderror to show the print output.
    fflush(stdout)
    fflush(stderr)
  }

  func testSendTrailers() throws {
    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
    let bufferFilterType = "type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer"
    let matcherTrailerName = "test-trailer"
    let matcherTrailerValue = "test.code"

    let expectation = self.expectation(description: "Run called with expected http status")

    EnvoyTestServer.startHttp1PlaintextServer()

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addNativeFilter(
        name: "envoy.filters.http.assertion",
        // swiftlint:disable:next line_length
        typedConfig: "[\(assertionFilterType)] {match_config: {http_request_trailers_match: {headers: [{name: '\(matcherTrailerName)', exact_match: '\(matcherTrailerValue)'}]}}}"
      )
      .addNativeFilter(
        name: "envoy.filters.http.buffer",
        typedConfig: "[\(bufferFilterType)] { max_request_bytes: { value: 65000 } }"
      )
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/simple.txt")
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
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

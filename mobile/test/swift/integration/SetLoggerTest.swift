import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class LoggerTests: XCTestCase {
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

  func testSetLogger() throws {
    let engineExpectation = self.expectation(description: "Run started engine")
    let loggingExpectation = self.expectation(description: "Run used platform logger")
    let logEventExpectation = self.expectation(
      description: "Run received log event via event tracker")

    EnvoyTestServer.startHttp1PlaintextServer()
    let port = String(EnvoyTestServer.getHttpPort())

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
        if msg.contains("starting main dispatch loop") {
          loggingExpectation.fulfill()
        }
      }
      .addNativeFilter(
        name: "test_logger",
        // swiftlint:disable:next line_length
        typedConfig: "[type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger]{}")
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .setEventTracker { event in
        if event["log_name"] == "event_name" {
          logEventExpectation.fulfill()
        }
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [loggingExpectation], timeout: 10), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/")
      .build()
    engine.streamClient()
      .newStreamPrototype()
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [logEventExpectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

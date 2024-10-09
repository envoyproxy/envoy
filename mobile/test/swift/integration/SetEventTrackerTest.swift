import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class SetEventTrackerTest: XCTestCase {
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

  func testSetEventTracker() throws {
    EnvoyTestServer.startHttp1PlaintextServer()

    let eventExpectation =
      self.expectation(description: "Passed event tracker receives an event")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
          print(msg, terminator: "")
      }
      .setEventTracker { event in
        if event["foo"] == "bar" {
          eventExpectation.fulfill()
        }
      }
      .addNativeFilter(
        name: "envoy.filters.http.test_event_tracker",
        // swiftlint:disable:next line_length
        typedConfig: "[type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker] { attributes: { key: 'foo' value: 'bar'}}")
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/simple.txt")
      .build()

    client
      .newStreamPrototype()
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [eventExpectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

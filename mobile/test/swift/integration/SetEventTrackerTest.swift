import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class EventTrackerTests: XCTestCase {
  func testSetEventTracker() throws {
    let engineExpectation = self.expectation(description: "Engine runs")
    let eventTrackingExpectation =
      self.expectation(description: "Passed event tracker receives an event")

    let client = EngineBuilder()
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .setEventTracker { event in
        XCTAssertEqual("bar", event["foo"])
        eventTrackingExpectation.fulfill()
      }

      .addNativeFilter(
        name: "envoy.filters.http.test_event_tracker",
        // swiftlint:disable:next line_length
        typedConfig: "{\"@type\":\"type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker\",\"attributes\":{\"foo\":\"bar\"}}")
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .build()

    client
      .newStreamPrototype()
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    let expectations = [engineExpectation, eventTrackingExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10, enforceOrder: true), .completed)
  }
}

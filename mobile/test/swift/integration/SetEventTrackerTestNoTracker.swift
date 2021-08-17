import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class SetEventTrackerTestNoTracker: XCTestCase {
  func testSetEventTracker() throws {
    let expectation = self.expectation(description: "Response headers received")

    let client = EngineBuilder()
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
      .setOnResponseHeaders { _, _, _ in
        expectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 10), .completed)
  }
}

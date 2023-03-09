@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class EnvoyEventTrackerFactoryTests: XCTestCase {
  func testBasic() {
    var received: [String: String]?
    let tracker = EnvoyEventTrackerFactory.create(track: { event in
      received = event
    })
    XCTAssertNil(received)
    tracker.track(["1": "2"].toEnvoyMap(), tracker.context)
    XCTAssertEqual(received, ["1": "2"])
  }
}

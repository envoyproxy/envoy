@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class EnvoyLoggerFactoryTests: XCTestCase {
  func testBasic() {
    var received: String?
    let logger = EnvoyLoggerFactory.create(log: { string in
      received = string
    })
    XCTAssertNil(received)
    logger.log("foo".toEnvoyData(), logger.context)
    XCTAssertEqual(received, "foo")
  }
}

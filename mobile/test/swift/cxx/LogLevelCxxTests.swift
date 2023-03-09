@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class LogLevelCxxTests: XCTestCase {
  func testToCxxDescription() {
    XCTAssertEqual(LogLevel.trace.toCXXDescription(), "T")
    XCTAssertEqual(LogLevel.debug.toCXXDescription(), "D")
    XCTAssertEqual(LogLevel.info.toCXXDescription(), "I")
    XCTAssertEqual(LogLevel.warn.toCXXDescription(), "W")
    XCTAssertEqual(LogLevel.error.toCXXDescription(), "E")
    XCTAssertEqual(LogLevel.critical.toCXXDescription(), "C")
    XCTAssertEqual(LogLevel.off.toCXXDescription(), "O")
  }
}

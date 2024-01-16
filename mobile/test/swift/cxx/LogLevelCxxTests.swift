@testable import Envoy
import XCTest

final class LogLevelCxxTests: XCTestCase {
  func testCxxDescription() {
    XCTAssertEqual(LogLevel.trace.cxxDescription, "trace")
    XCTAssertEqual(LogLevel.debug.cxxDescription, "debug")
    XCTAssertEqual(LogLevel.info.cxxDescription, "info")
    XCTAssertEqual(LogLevel.warn.cxxDescription, "warn")
    XCTAssertEqual(LogLevel.error.cxxDescription, "error")
    XCTAssertEqual(LogLevel.critical.cxxDescription, "critical")
    XCTAssertEqual(LogLevel.off.cxxDescription, "off")
  }
}

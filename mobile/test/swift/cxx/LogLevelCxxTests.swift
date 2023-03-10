@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class LogLevelCxxTests: XCTestCase {
  func testToCxxDescription() {
    XCTAssertEqual(LogLevel.trace.toCXXDescription(), "trace")
    XCTAssertEqual(LogLevel.debug.toCXXDescription(), "debug")
    XCTAssertEqual(LogLevel.info.toCXXDescription(), "info")
    XCTAssertEqual(LogLevel.warn.toCXXDescription(), "warn")
    XCTAssertEqual(LogLevel.error.toCXXDescription(), "error")
    XCTAssertEqual(LogLevel.critical.toCXXDescription(), "critical")
    XCTAssertEqual(LogLevel.off.toCXXDescription(), "off")
  }
}

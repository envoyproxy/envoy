@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class EnvoyHeadersTests: XCTestCase {
  func testToSwiftHeadersEmptyInput() {
    let input = envoy_noheaders
    let output = toSwiftHeaders(input)
    XCTAssertEqual(output, [:])
  }

  func testRoundtrip() {
    let input: EnvoyHeaders = ["1": ["2", "3"], "4": ["5"]]
    let middle = toEnvoyHeaders(input)
    let output = toSwiftHeaders(middle)
    XCTAssertEqual(output, ["1": ["2", "3"], "4": ["5"]])
  }

  func testRoundtripLosesEmptyValues() {
    let input: EnvoyHeaders = ["1": ["2", "3"], "4": []]
    let middle = toEnvoyHeaders(input)
    let output = toSwiftHeaders(middle)
    XCTAssertEqual(output, ["1": ["2", "3"]])
  }

  func testToEnvoyHeadersPtrWithNilInput() {
    XCTAssertNil(toEnvoyHeadersPtr(nil))
  }

  func testToEnvoyHeadersPtrWithValueInput() throws {
    let input: EnvoyHeaders = ["1": ["2", "3"], "4": ["5"]]
    let middlePtr = try XCTUnwrap(toEnvoyHeadersPtr(input))
    let middle = middlePtr.pointee
    let output = toSwiftHeaders(middle)
    XCTAssertEqual(output, ["1": ["2", "3"], "4": ["5"]])
  }
}

@testable import Envoy
import XCTest

typealias TransformClosure<T, U> = (T) -> U

final class PointerBoxTests: XCTestCase {
  func testRoundtripString() {
    let input = "input"
    let inputBox = PointerBox(value: input)
    let inputPointer = inputBox.retainedPointer()
    let output = PointerBox<String>.unretained(from: inputPointer)
    XCTAssertEqual(output, "input")
    PointerBox<String>.unmanaged(from: inputPointer).release()
  }

  func testRoundtripClosure() {
    let input: TransformClosure<String, Int> = { $0.count }
    let inputBox = PointerBox(value: input)
    let inputPointer = inputBox.retainedPointer()
    let output = PointerBox<TransformClosure<String, Int>>.unretained(from: inputPointer)
    XCTAssertEqual(output("foobar"), 6)
    PointerBox<String>.unmanaged(from: inputPointer).release()
  }
}

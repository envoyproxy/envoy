import Envoy
@_implementationOnly import FlatBuffers
import XCTest

final class FlatBufferTest: XCTestCase {
    func testCreateFlatBuffer() {
        // This test simply verifies that we can import both the generated types as
        // well as upstream FlatBuffers.
        _ = Test_Nested_SomeTypeT()
        _ = FlatBufferBuilder()
    }
}

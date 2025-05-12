import Envoy
import XCTest

final class ElementTests: XCTestCase {
    func testElementNotEqualToObjectOfUnrelatedType() {
        let element = Element("foo")
        XCTAssertFalse(element.isEqual("bar"))
    }

    func testElementsAreEqualIfStringValuesAreEqual() {
      XCTAssertTrue(Element("foo").isEqual(Element("foo")))
      XCTAssertEqual(Element("foo"), Element("foo"))

      XCTAssertFalse(Element("foo").isEqual(Element("bar")))
      XCTAssertNotEqual(Element("foo"), Element("bar"))
    }
}

@testable import Envoy
import XCTest

final class TagsBuilderTests: XCTestCase {
  func testAddsTagToTags() {
    let tags = TagsBuilder().add(name: "testKey", value: "testValue").build()
    XCTAssertEqual(tags.allTags(), ["testKey": "testValue"])
  }

  func testSetsTagToTags() {
    let tags = TagsBuilder()
              .add(name: "testKey", value: "testValue1")
              .set(name: "testKey", value: "testValue2")
              .build()
    XCTAssertEqual(tags.allTags(), ["testKey": "testValue2"])
  }

  func testRemovesTagFromTags() {
    let tags = TagsBuilder()
              .add(name: "testKey1", value: "testValue1")
              .add(name: "testKey2", value: "testValue2")
              .remove(name: "testKey1")
              .build()
    XCTAssertEqual(tags.allTags(), ["testKey2": "testValue2"])
  }

  func testPutAllTagToTags() {
    let tagsMap = ["testKey1": "testValue1", "testKey2": "testValue2"]
    let tags = TagsBuilder()
              .putAll(tags: tagsMap)
              .build()
    XCTAssertEqual(tags.allTags(), ["testKey1": "testValue1", "testKey2": "testValue2"])
  }
}

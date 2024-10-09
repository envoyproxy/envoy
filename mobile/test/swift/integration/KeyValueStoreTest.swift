import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class KeyValueStoreTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  override static func tearDown() {
    super.tearDown()
    // Flush the stdout and stderror to show the print output.
    fflush(stdout)
    fflush(stderr)
  }

  func testKeyValueStore() {
    // swiftlint:disable:next line_length
    let kvStoreType = "type.googleapis.com/envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore"
    let readExpectation = self.expectation(description: "Read called on key-value store")
    // Called multiple times for validation in test filter.
    readExpectation.assertForOverFulfill = false
    let saveExpectation = self.expectation(description: "Save called on key-value store")

    class TestKeyValueStore: KeyValueStore {
      let readExpectation: XCTestExpectation
      let saveExpectation: XCTestExpectation

      init(readExpectation: XCTestExpectation, saveExpectation: XCTestExpectation) {
        self.readExpectation = readExpectation
        self.saveExpectation = saveExpectation
      }

      func readValue(forKey: String) -> String? { readExpectation.fulfill(); return nil }
      func saveValue(_: String, toKey: String) { saveExpectation.fulfill() }
      func removeKey(_: String) {}
    }

    let testStore = TestKeyValueStore(readExpectation: readExpectation,
                                      saveExpectation: saveExpectation)

    EnvoyTestServer.startHttp1PlaintextServer()

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addKeyValueStore(
        name: "envoy.key_value.platform_test",
        keyValueStore: testStore
      )
      .addNativeFilter(
        name: "envoy.filters.http.test_kv_store",
        // swiftlint:disable:next line_length
        typedConfig: "[\(kvStoreType)]{ kv_store_name: 'envoy.key_value.platform_test', test_key: 'foo', test_value: 'bar'}"
      )
      .addRuntimeGuard("test_feature_false", true)
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "http",
      authority: "localhost:" + String(EnvoyTestServer.getHttpPort()), path: "/simple.txt"
    )
    .build()

    client
      .newStreamPrototype()
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(
      XCTWaiter.wait(for: [readExpectation, saveExpectation], timeout: 10),
      .completed
    )

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}

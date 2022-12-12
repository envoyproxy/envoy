import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class KeyValueStoreTests: XCTestCase {
  func testKeyValueStore() {
    // swiftlint:disable:next line_length
    let ehcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    // swiftlint:disable:next line_length
    let kvStoreType = "type.googleapis.com/envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore"
    let testKey = "foo"
    let testValue = "bar"
    let config =
"""
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": \(ehcmType)
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
              - name: api
                domains:
                  - "*"
                routes:
                  - match:
                      prefix: "/"
                    direct_response:
                      status: 200
          http_filters:
            - name: envoy.filters.http.test_kv_store
              typed_config:
                "@type": \(kvStoreType)
                kv_store_name: envoy.key_value.platform_test
                test_key: \(testKey)
                test_value: \(testValue)
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

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

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addKeyValueStore(
        name: "envoy.key_value.platform_test",
        keyValueStore: testStore
      )
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "https",
      authority: "example.com", path: "/test"
    )
    .addUpstreamHttpProtocol(.http2)
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
  }
}

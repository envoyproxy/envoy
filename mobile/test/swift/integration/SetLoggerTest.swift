import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class LoggerTests: XCTestCase {
  func testSetLogger() throws {
    // swiftlint:disable:next line_length
    let apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
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
        "@type": \(apiListenerType)
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
          - name: envoy.filters.http.assertion
            typed_config:
              "@type": \(assertionFilterType)
              match_config:
                http_request_headers_match:
                  headers:
                    - name: ":authority"
                      exact_match: example.com
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

    let engineExpectation = self.expectation(description: "Run started engine")
    let loggingExpectation = self.expectation(description: "Run used platform logger")

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .setLogger { msg in
        if msg.contains("starting main dispatch loop") {
          loggingExpectation.fulfill()
        }
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 1), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [loggingExpectation], timeout: 1), .completed)

    engine.terminate()
  }
}

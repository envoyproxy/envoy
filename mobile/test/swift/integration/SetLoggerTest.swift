import Envoy
import EnvoyEngine
import Foundation
import TestExtensions
import XCTest

final class LoggerTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testSetLogger() throws {
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
    // swiftlint:disable:next line_length
    let testLoggerFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger"
    let config =
"""
listener_manager:
    name: envoy.listener_manager_impl.api
    typed_config:
      "@type": type.googleapis.com/envoy.config.listener.v3.ApiListenerManager
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
        "@type": \(emhcmType)
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
            - name: test_logger
              typed_config:
                "@type": \(testLoggerFilterType)
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
    let logEventExpectation = self.expectation(
      description: "Run received log event via event tracker")

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .setLogger { _, msg in
        if msg.contains("starting main dispatch loop") {
          loggingExpectation.fulfill()
        }
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .setEventTracker { event in
        if event["log_name"] == "event_name" {
          logEventExpectation.fulfill()
        }
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [loggingExpectation], timeout: 10), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .build()
    engine.streamClient()
      .newStreamPrototype()
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [logEventExpectation], timeout: 10), .completed)

    engine.terminate()
  }
}

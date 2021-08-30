import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class DrainConnectionsTest: XCTestCase {
  func testDrainConnections() {
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
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
    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()

    let expectation1 =
      self.expectation(description: "Run called with expected http status first request")

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation1.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation1], timeout: 1), .completed)

    engine.drainConnections()

    let expectation2 =
      self.expectation(description: "Run called with expected http status first request")

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation2.fulfill()
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation2], timeout: 1), .completed)

    engine.terminate()
  }
}

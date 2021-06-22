import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class SendDataTests: XCTestCase {
  func testSendData() throws {
    // swiftlint:disable:next line_length
    let apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    // swiftlint:disable:next line_length
    let assertionFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion"
    let requestStringMatch = "match_me"
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
                http_request_generic_body_match:
                  patterns:
                    - string_match: \(requestStringMatch)
          - name: envoy.filters.http.buffer
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
              max_request_bytes: 65000
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""
    let expectation = self.expectation(description: "Run called with expected http status")
    let client = EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .addPlatformFilter(DemoFilter.init)
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()
    let body = try XCTUnwrap(requestStringMatch.data(using: .utf8))

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         XCTAssertTrue(endStream)
         expectation.fulfill()
      }
      .setOnError { _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .close(data: body)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 1), .completed)
  }
}

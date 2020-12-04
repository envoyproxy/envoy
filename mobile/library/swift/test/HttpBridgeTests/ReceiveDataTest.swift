import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class ReceiveDataTests: XCTestCase {
  func testReceiveData() throws {
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
                        body:
                          inline_string: response_body
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
    let client = try EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .addPlatformFilter(factory: DemoFilter.init)
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()

    let headersExpectation = self.expectation(description: "Run called with expected headers")
    let dataExpectation = self.expectation(description: "Run called with expected data")

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         headersExpectation.fulfill()
      }
      .setOnResponseData { data, _ in
        let responseBody = String(data: data, encoding: .utf8)
        XCTAssertEqual("response_body", responseBody)
        dataExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [headersExpectation, dataExpectation], timeout: 1,
                                  enforceOrder: true),
                   .completed)
  }
}

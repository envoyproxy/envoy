import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class SendTrailersTests: XCTestCase {
  func testSendTrailers() throws {
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
                    http_request_trailers_match:
                      headers:
                        - name: "test-trailer"
                          exact_match: test.code
              - name: envoy.filters.http.buffer
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
                  max_request_bytes: 65000
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    """
    let expectation = self.expectation(description: "Run called with expected http status")
    let client = try EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .addFilter(factory: DemoFilter.init)
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()
    let body = try XCTUnwrap("match_me".data(using: .utf8))
    let requestTrailers = RequestTrailersBuilder()
      .add(name: "test-trailer", value: "test.code")
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         expectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .sendData(body)
      .close(trailers: requestTrailers)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 1), .completed)
  }
}

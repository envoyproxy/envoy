import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class ReceiveErrorTests: XCTestCase {
  func testReceiveError() throws {
    // swiftlint:disable:next line_length
    let apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    // swiftlint:disable:next line_length
    let localErrorFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
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
                        status: 503
            http_filters:
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": \(localErrorFilterType)
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    """
    let expectation = self.expectation(description: "Run called with expected error")
    let client = try EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .addPlatformFilter(factory: DemoFilter.init)
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()
    client
      .newStreamPrototype()
      .setOnResponseHeaders { _, _ in
        XCTFail("Headers received instead of expected error")
      }
      .setOnResponseData { _, _ in
        XCTFail("Data received instead of expected error")
      }
      // The unmatched expecation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { error in
         XCTAssertEqual(error.errorCode, 2) // 503/Connection Failure
         expectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 1), .completed)
  }
}

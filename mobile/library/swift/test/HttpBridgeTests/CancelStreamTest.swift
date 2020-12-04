import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class CancelStreamTests: XCTestCase {
  func testCancelStream() throws {
    // swiftlint:disable:next line_length
    let apiListenerType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
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
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    """
    let expectation = self.expectation(description: "Run called with expected cancellation")
    let client = try EngineBuilder(yaml: config)
      .addLogLevel(.debug)
      .addPlatformFilter(factory: DemoFilter.init)
      .build()
      .streamClient()

    client
      .newStreamPrototype()
      .setOnCancel {
         expectation.fulfill()
      }
      .start()
      .cancel()

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 1), .completed)
  }
}

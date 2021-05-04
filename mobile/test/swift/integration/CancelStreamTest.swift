import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class CancelStreamTests: XCTestCase {
  func testCancelStream() {
    // swiftlint:disable:next line_length
    let hcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    // swiftlint:disable:next line_length
    let pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
    let config =
    """
    static_resources:
      listeners:
      - name: fake_remote_listener
        address:
          socket_address: { protocol: TCP, address: 127.0.0.1, port_value: 10101 }
        filter_chains:
        - filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": \(hcmType)
              stat_prefix: remote_hcm
              route_config:
                name: remote_route
                virtual_hosts:
                - name: remote_service
                  domains: ["*"]
                  routes:
                  - match: { prefix: "/" }
                    direct_response: { status: 200 }
              http_filters:
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      - name: base_api_listener
        address:
          socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
        api_listener:
          api_listener:
            "@type": \(hcmType)
            stat_prefix: api_hcm
            route_config:
              name: api_router
              virtual_hosts:
              - name: api
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: fake_remote }
            http_filters:
            - name: envoy.filters.http.platform_bridge
              typed_config:
                "@type": \(pbfType)
                platform_filter_name: cancel_validation_filter
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      clusters:
      - name: fake_remote
        connect_timeout: 0.25s
        type: STATIC
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: fake_remote
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address: { address: 127.0.0.1, port_value: 10101 }
    """

    struct CancelValidationFilter: ResponseFilter {
      let expectation: XCTestExpectation

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .continue(headers: headers)
      }

      func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus<ResponseHeaders> {
        return .continue(data: body)
      }

      func onResponseTrailers(_ trailers: ResponseTrailers)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        return .continue(trailers: trailers)
      }

      func onError(_ error: EnvoyError) {}

      func onCancel() {
        self.expectation.fulfill()
      }
    }

    let runExpectation = self.expectation(description: "Run called with expected cancellation")
    let filterExpectation = self.expectation(description: "Filter called with cancellation")

    let client = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: "cancel_validation_filter",
        factory: { CancelValidationFilter(expectation: filterExpectation) }
      )
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()

    client
      .newStreamPrototype()
      .setOnCancel {
         runExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .cancel()

    XCTAssertEqual(XCTWaiter.wait(for: [filterExpectation, runExpectation], timeout: 1), .completed)
  }
}

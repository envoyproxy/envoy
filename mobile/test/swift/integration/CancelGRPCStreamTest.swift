import Envoy
import EnvoyEngine
import Foundation
import TestExtensions
import XCTest

final class CancelGRPCStreamTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testCancelGRPCStream() {
    let filterName = "cancel_validation_filter"

// swiftlint:disable line_length
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
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
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
          - name: envoy.filters.http.local_error
            typed_config:
              "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
          - name: envoy.filters.http.platform_bridge
            typed_config:
              "@type": type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge
              platform_filter_name: \(filterName)
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
              socket_address: { address: 127.0.0.1, port_value: \(Int.random(in: 10001...11000)) }
"""
// swiftlint:enable line_length

    struct CancelValidationFilter: ResponseFilter {
      let expectation: XCTestExpectation

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .continue(headers: headers)
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        return .continue(data: body)
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        return .continue(trailers: trailers)
      }

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {}

      func onCancel(streamIntel: FinalStreamIntel) {
        self.expectation.fulfill()
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let onCancelCallbackExpectation = self.expectation(description: "onCancel callback called")
    let filterExpectation = self.expectation(description: "Filter called with cancellation")

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: filterName,
        factory: { CancelValidationFilter(expectation: filterExpectation) }
      )
      .build()

    let client = GRPCClient(streamClient: engine.streamClient())

    let requestHeaders = GRPCRequestHeadersBuilder(
        scheme: "https",
        authority: "example.com", path: "/test")
      .build()

    client
      .newGRPCStreamPrototype()
      .setOnCancel { _ in
         onCancelCallbackExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .cancel()

    let expectations = [onCancelCallbackExpectation, filterExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
  }
}

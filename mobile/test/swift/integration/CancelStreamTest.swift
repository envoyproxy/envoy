import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class CancelStreamTests: XCTestCase {
  func testCancelStream() {
    let remotePort = Int.random(in: 10001...11000)
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    let lefType = "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
    // swiftlint:disable:next line_length
    let pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
    let filterName = "cancel_validation_filter"
    let config =
"""
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": \(emhcmType)
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
              "@type": \(lefType)
          - name: envoy.filters.http.platform_bridge
            typed_config:
              "@type": \(pbfType)
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
              socket_address: { address: 127.0.0.1, port_value: \(remotePort) }
"""

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

    let runExpectation = self.expectation(description: "Run called with expected cancellation")
    let filterExpectation = self.expectation(description: "Filter called with cancellation")

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: filterName,
        factory: { CancelValidationFilter(expectation: filterExpectation) }
      )
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()

    client
      .newStreamPrototype()
      .setOnCancel { _ in
         runExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .cancel()

    XCTAssertEqual(XCTWaiter.wait(for: [filterExpectation, runExpectation], timeout: 3), .completed)

    engine.terminate()
  }
}

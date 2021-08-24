import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class IdleTimeoutTests: XCTestCase {
  func testIdleTimeout() {
    let idleTimeout = "0.5s"
    // swiftlint:disable:next line_length
    let hcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    let lefType = "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
    // swiftlint:disable:next line_length
    let pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
    let filterName = "idle_timeout_validation_filter"
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
        stream_idle_timeout: \(idleTimeout)
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
              socket_address: { address: 127.0.0.1, port_value: 10101 }
"""

    class IdleTimeoutValidationFilter: AsyncResponseFilter {
      let timeoutExpectation: XCTestExpectation
      var callbacks: ResponseFilterCallbacks!

      init(timeoutExpectation: XCTestExpectation) {
        self.timeoutExpectation = timeoutExpectation
      }

      func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks) {
        self.callbacks = callbacks
      }

      func onResumeResponse(
        headers: ResponseHeaders?,
        data: Data?,
        trailers: ResponseTrailers?,
        endStream: Bool,
        streamIntel: StreamIntel
      ) -> FilterResumeStatus<ResponseHeaders, ResponseTrailers> {
        XCTFail("Unexpected call to onResumeResponse")
        return .resumeIteration(headers: nil, data: nil, trailers: nil)
      }

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .stopIteration
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        XCTFail("Unexpected call to onResponseData filter callback")
        return .stopIterationNoBuffer
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        XCTFail("Unexpected call to onResponseTrailers filter callback")
        return .stopIteration
      }

      func onError(_ error: EnvoyError, streamIntel: StreamIntel) {
        XCTAssertEqual(error.errorCode, 4)
        timeoutExpectation.fulfill()
      }

      func onCancel(streamIntel: StreamIntel) {
        XCTFail("Unexpected call to onCancel filter callback")
      }
    }

    let filterExpectation = self.expectation(description: "Stream idle timeout received by filter")
    let callbackExpectation =
      self.expectation(description: "Stream idle timeout received by callbacks")

    let client = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: filterName,
        factory: { IdleTimeoutValidationFilter(timeoutExpectation: filterExpectation) }
      )
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .addUpstreamHttpProtocol(.http2)
      .build()

    client
      .newStreamPrototype()
      .setOnError { error, _ in
        XCTAssertEqual(error.errorCode, 4)
        callbackExpectation.fulfill()
      }
      .setOnCancel { _ in
        XCTFail("Unexpected call to onCancel filter callback")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(
      XCTWaiter.wait(for: [filterExpectation, callbackExpectation], timeout: 2),
      .completed
    )
  }
}

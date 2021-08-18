import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class FilterResetIdleTests: XCTestCase {
  func skipped_testFilterResetIdle() {
    let idleTimeout = "0.5s"
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    let hcmType =
      // swiftlint:disable:next line_length
      "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    let pbfType =
      "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
    let localErrorFilterType =
      "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
    let filterName = "reset_idle_test_filter"
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
              "@type": \(emhcmType)
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
              - name: envoy.filters.http.platform_bridge
                typed_config:
                  "@type": \(pbfType)
                  platform_filter_name: \(filterName)
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": \(localErrorFilterType)
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

    class ResetIdleTestFilter: AsyncRequestFilter, ResponseFilter {
      let queue = DispatchQueue(label: "io.envoyproxy.async")
      let resetExpectation: XCTestExpectation
      let cancelExpectation: XCTestExpectation
      var callbacks: RequestFilterCallbacks!
      var resetCount = 0

      // Trigger 3 idle timer exception, then allow timeout
      private func signalActivity() {
        if resetCount < 3 {
          self.resetCount += 1
          self.queue.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let strongSelf = self else {
              return
            }
            strongSelf.callbacks.resetIdleTimer()
            strongSelf.signalActivity()
          }
        } else {
          resetExpectation.fulfill()
        }
      }

      init(resetExpectation: XCTestExpectation, cancelExpectation: XCTestExpectation) {
        self.resetExpectation = resetExpectation
        self.cancelExpectation = cancelExpectation
      }

      func setRequestFilterCallbacks(_ callbacks: RequestFilterCallbacks) {
        self.callbacks = callbacks
      }

      func onResumeRequest(
        headers: RequestHeaders?,
        data: Data?,
        trailers: RequestTrailers?,
        endStream: Bool,
        streamIntel: StreamIntel
      ) -> FilterResumeStatus<RequestHeaders, RequestTrailers> {
        XCTFail("Unexpected call to onResumeRequest")
        return .resumeIteration(headers: nil, data: nil, trailers: nil)
      }

      func onRequestHeaders(_ headers: RequestHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<RequestHeaders>
      {
        self.signalActivity()
        return .stopIteration
      }

      func onRequestData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<RequestHeaders>
      {
        XCTFail("Unexpected call to onRequestData filter callback")
        return .stopIterationNoBuffer
      }

      func onRequestTrailers(_ trailers: RequestTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<RequestHeaders, RequestTrailers>
      {
        XCTFail("Unexpected call to onRequestTrailers filter callback")
        return .stopIteration
      }

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        self.signalActivity()
        return .stopIteration
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        XCTFail("Unexpected call to onResponseData filter callback")
        return .stopIterationNoBuffer
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers>
      {
        XCTFail("Unexpected call to onResponseTrailers filter callback")
        return .stopIteration
      }

      func onError(_ error: EnvoyError, streamIntel: StreamIntel) {}

      func onCancel(streamIntel: StreamIntel) {
        cancelExpectation.fulfill()
      }
    }

    let resetExpectation = self.expectation(description: "Stream idle timer reset 3 times")
    let timeoutExpectation = self.expectation(description: "Stream idle timeout triggered")
    let cancelExpectation = self.expectation(
      description: "Stream cancellation triggered incorrectly")
    cancelExpectation.isInverted = true

    let client = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: filterName,
        factory: {
          ResetIdleTestFilter(
            resetExpectation: resetExpectation, cancelExpectation: cancelExpectation)
        }
      )
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "https",
      authority: "example.com", path: "/test"
    )
    .addUpstreamHttpProtocol(.http2)
    .build()

    client
      .newStreamPrototype()
      .setOnError { _, _ in
        timeoutExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(
      XCTWaiter.wait(for: [resetExpectation, timeoutExpectation], timeout: 10),
      .completed
    )

    XCTAssertEqual(
      XCTWaiter.wait(for: [cancelExpectation], timeout: 1),
      .completed
    )
  }
}

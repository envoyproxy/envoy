#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"

#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASANITIZED /* Sanitized by Clang */
#endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ASANITIZED /* Sanitized by GCC */
#endif

class CrashIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

// Insufficient support on Windows.
#ifndef WIN32
// ASAN hijacks the signal handlers, so the process will die but not output
// the particular messages we expect.
#ifndef ASANITIZED
// If we don't install any signal handlers (i.e. due to compile options), we
// won't get the crash report.
#ifdef ENVOY_HANDLE_SIGNALS

// Tests should run with all protocols.
class CrashIntegrationTestAllProtocols : public CrashIntegrationTest {};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, CrashIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CrashIntegrationTestAllProtocols, UnwindsTrackedObjectStack) {
  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
      )EOF";
  initializeFilter(request_kill_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test"},
                                                       {":scheme", "http"},
                                                       {":authority", "host"},
                                                       {"x-envoy-kill-request", "true"}};
  // We should have the following directly on the tracked object stack and should dump them on
  // crash:
  //  - ActiveStream
  //  - Http(1|2)::ConnectionImpl
  //  - Network::ConnectionImpl
  const std::string death_string = GetParam().downstream_protocol == Http::CodecType::HTTP2
                                       ? "ActiveStream.*Http2::ConnectionImpl.*ConnectionImpl"
                                       : "ActiveStream.*Http1::ConnectionImpl.*ConnectionImpl";
  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               death_string);
}

TEST_P(CrashIntegrationTestAllProtocols, ResponseCrashDumpsTheCorrespondingRequest) {
  const std::string response_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
        direction: RESPONSE
      )EOF";
  initializeFilter(response_kill_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  const Http::TestResponseHeaderMapImpl kill_response_headers = {{":status", "200"},
                                                                 {"x-envoy-kill-request", "true"}};
  // Check that we dump the downstream request
  EXPECT_DEATH(
      sendRequestAndWaitForResponse(default_request_headers_, 0, kill_response_headers, 1024),
      "Dumping corresponding downstream request.*UpstreamRequest.*request_headers:");
}

TEST_P(CrashIntegrationTestAllProtocols, DecodeContinueDoesNotAddTrackedObjectIfExists) {
  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
      )EOF";
  config_helper_.prependFilter(request_kill_config);

  // This will stop iteration, and continue via a callback.
  const std::string stop_and_continue_config = R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
    installScopeTrackedObject: true
  )EOF";
  config_helper_.prependFilter(stop_and_continue_config);

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test"},
                                                       {":scheme", "http"},
                                                       {":authority", "host"},
                                                       {"x-envoy-kill-request", "true"}};
  // We should have the following directly on the tracked object stack and should dump them on
  // crash:
  // - The filter's custom scope tracked object
  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               "StopIterationAndContinue decode_delay_timer");
}

TEST_P(CrashIntegrationTestAllProtocols, DecodeContinueAddsCrashContextIfNoneExists) {
  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
      )EOF";
  config_helper_.prependFilter(request_kill_config);

  // This will stop iteration, and continue via a callback.
  const std::string stop_and_continue_config = R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
    installScopeTrackedObject: false
  )EOF";
  config_helper_.prependFilter(stop_and_continue_config);

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test"},
                                                       {":scheme", "http"},
                                                       {":authority", "host"},
                                                       {"x-envoy-kill-request", "true"}};
  // We should should dump the following by the ScopeTrackedObject installed in
  // commonContinue:
  //  - ActiveStream
  //  - Network::ConnectionImpl
  EXPECT_DEATH(sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 1024),
               "ActiveStream.*.*ConnectionImpl");
}

TEST_P(CrashIntegrationTestAllProtocols, EncodeContinueDoesNotAddTrackedObjectIfExists) {
  // This will stop iteration, and continue via a callback.
  const std::string stop_and_continue_config = R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
    installScopeTrackedObject: true
  )EOF";
  config_helper_.prependFilter(stop_and_continue_config);

  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
        direction: RESPONSE
      )EOF";
  config_helper_.prependFilter(request_kill_config);

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestResponseHeaderMapImpl kill_response_headers = {{":status", "200"},
                                                                 {"x-envoy-kill-request", "true"}};
  // We should have the following directly on the tracked object stack and should dump them on
  // crash:
  // - The filter's custom scope tracked object
  EXPECT_DEATH(sendRequestAndWaitForResponse(default_request_headers_, 0, kill_response_headers, 0),
               "StopIterationAndContinue encode_delay_timer");
}

TEST_P(CrashIntegrationTestAllProtocols, EncodeContinueAddsCrashContextIfNoneExists) {
  // This will stop iteration, and continue via a callback.
  const std::string stop_and_continue_config = R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
    installScopeTrackedObject: false
  )EOF";
  config_helper_.prependFilter(stop_and_continue_config);

  const std::string request_kill_config =
      R"EOF(
      name: envoy.filters.http.kill_request
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
        probability:
          numerator: 100
        direction: RESPONSE
      )EOF";
  config_helper_.prependFilter(request_kill_config);

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestResponseHeaderMapImpl kill_response_headers = {{":status", "200"},
                                                                 {"x-envoy-kill-request", "true"}};
  // We should should dump the following by the ScopeTrackedObject installed in
  // commonContinue:
  //  - ActiveStream
  //  - Network::ConnectionImpl
  EXPECT_DEATH(sendRequestAndWaitForResponse(default_request_headers_, 0, kill_response_headers, 0),
               "ActiveStream.*.*ConnectionImpl");
}

#endif
#endif
#endif

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

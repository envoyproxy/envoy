#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Wasm {
namespace {

using testing::Ge;

class WasmFilterIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<std::string, std::string, bool, Http::CodecType>> {
public:
  WasmFilterIntegrationTest()
      : HttpIntegrationTest(std::get<3>(GetParam()), Network::Address::IpVersion::v4) {}

  void SetUp() override {
    setUpstreamProtocol(std::get<3>(GetParam()));
    if (std::get<3>(GetParam()) == Http::CodecType::HTTP2) {
      config_helper_.setClientCodec(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::HTTP2);
    } else {
      config_helper_.setClientCodec(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::HTTP1);
    }
    // Wasm filters are expensive to setup and sometime default is not enough,
    // It needs to increase timeout to avoid flaky tests
    setListenersBoundTimeout(30 * TestUtility::DefaultTimeout);
  }

  void TearDown() override { fake_upstream_connection_.reset(); }

  void setupWasmFilter(const std::string& config, const std::string& root_id = "") {
    bool downstream = std::get<2>(GetParam());
    const std::string yaml = TestEnvironment::substitute(absl::StrCat(
        R"EOF(
          name: envoy.filters.http.wasm
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm
            config:
              name: "plugin_name"
              root_id: ")EOF",
        root_id, R"EOF("
              vm_config:
                vm_id: "vm_id"
                runtime: envoy.wasm.runtime.)EOF",
        std::get<0>(GetParam()), R"EOF(
                configuration:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: )EOF",
        config, R"EOF(
                code:
                  local:
                    filename: "{{ test_rundir }}/test/extensions/filters/http/wasm/test_data/test_cpp.wasm"
        )EOF"));
    config_helper_.prependFilter(yaml, downstream);

    // Set buffer limit for HTTP1 to same value with the default limit of HTTP2.
    config_helper_.setBufferLimits(65535, 65535);
  }

  template <typename TMap> void assertCompareMaps(const TMap& m1, const TMap& m2) {
    m1.iterate([&m2](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
      Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
      if (entry.value().empty()) {
        EXPECT_TRUE(m2.get(lower_key).empty());
      } else {
        if (m2.get(lower_key).empty()) {
          ADD_FAILURE() << "Header " << lower_key.get() << " not found.";
        } else {
          EXPECT_EQ(entry.value().getStringView(), m2.get(lower_key)[0]->value().getStringView());
        }
      }
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  void runTest(const Http::RequestHeaderMap& request_headers,
               const std::vector<std::string>& request_body,
               const Http::RequestHeaderMap& expected_request_headers,
               const std::string& expected_request_body,
               const Http::ResponseHeaderMap& upstream_response_headers,
               const std::vector<std::string>& upstream_response_body,
               const Http::ResponseHeaderMap& expected_response_headers,
               const std::string& expected_response_body) {

    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      const auto request_body_size = request_body.size();
      for (size_t n = 0; n < request_body_size; n++) {
        Buffer::OwnedImpl buffer(request_body[n]);
        codec_client_->sendData(*request_encoder_, buffer, n == request_body_size - 1);
      }
    }

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    assertCompareMaps(expected_request_headers, upstream_request_->headers());
    EXPECT_STREQ(expected_request_body.c_str(), upstream_request_->body().toString().c_str());

    if (upstream_response_body.empty()) {
      upstream_request_->encodeHeaders(upstream_response_headers, true /*end_stream*/);
    } else {
      upstream_request_->encodeHeaders(upstream_response_headers, false /*end_stream*/);
      const auto upstream_response_body_size = upstream_response_body.size();
      for (size_t n = 0; n < upstream_response_body_size; n++) {
        Buffer::OwnedImpl buffer(upstream_response_body[n]);
        upstream_request_->encodeData(buffer, n == upstream_response_body_size - 1);
      }
    }

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    assertCompareMaps(expected_response_headers, response->headers());
    EXPECT_STREQ(expected_response_body.c_str(), response->body().c_str());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmFilterIntegrationTest,
                         Common::Wasm::dual_filter_with_codecs_sandbox_runtime_and_cpp_values,
                         Common::Wasm::wasmDualFilterWithCodecsTestParamsToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WasmFilterIntegrationTest);

TEST_P(WasmFilterIntegrationTest, HeadersManipulation) {
  setupWasmFilter("headers");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"server", "client-id"}};
  Http::TestRequestHeaderMapImpl expected_request_headers{{":method", "GET"},
                                                          {":path", "/resize?type=jpg"},
                                                          {":authority", "host"},
                                                          {"newheader", "newheadervalue"},
                                                          {"server", "envoy-wasm"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"content-type", "application/json"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {":status", "200"}, {"content-type", "application/json"}, {"test-status", "OK"}};

  auto request_body = std::vector<std::string>{};
  auto upstream_response_body = std::vector<std::string>{};
  runTest(request_headers, request_body, expected_request_headers, "", upstream_response_headers,
          upstream_response_body, expected_response_headers, "");
}

TEST_P(WasmFilterIntegrationTest, BodyManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "ReplaceBufferedBody"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{
      {":status", "200"}, {"x-test-operation", "ReplaceBufferedBody"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_body"}};
  auto upstream_response_body = std::vector<std::string>{{"upstream_body"}};
  runTest(request_headers, request_body, expected_request_headers, "replace",
          upstream_response_headers, upstream_response_body, expected_response_headers, "replace");
}

TEST_P(WasmFilterIntegrationTest, BodyBufferedManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "SetEndOfBodies"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"x-test-operation", "SetEndOfBodies"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_"}, {"body"}};
  auto upstream_response_body = std::vector<std::string>{{"upstream_"}, {"body"}};
  runTest(request_headers, request_body, expected_request_headers, "request_body.end",
          upstream_response_headers, upstream_response_body, expected_response_headers,
          "upstream_body.end");
}

TEST_P(WasmFilterIntegrationTest, BodyBufferedMultipleChunksManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "SetEndOfBodies"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"x-test-operation", "SetEndOfBodies"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_"}, {"very_"}, {"long_"}, {"body"}};
  auto upstream_response_body =
      std::vector<std::string>{{"upstream_"}, {"very_"}, {"long_"}, {"body"}};
  runTest(request_headers, request_body, expected_request_headers, "request_very_long_body.end",
          upstream_response_headers, upstream_response_body, expected_response_headers,
          "upstream_very_long_body.end");
}

TEST_P(WasmFilterIntegrationTest, PanicReturn503) {
  setupWasmFilter("", "panic");
  HttpIntegrationTest::initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(WasmFilterIntegrationTest, LargeRequestHitBufferLimit) {
  // TODO(wbpcode): upstream HTTP filter couldn't stop the iteration correctly.
  if (bool downstream = std::get<2>(GetParam()); !downstream) {
    return;
  }

  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "POST"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "SetEndOfBodies"}};
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  std::string large_body(65535, 'a');
  codec_client_->sendData(*request_encoder_, large_body, false);
  // Wait the previous data is sent actually.
  timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(50));

  codec_client_->sendData(*request_encoder_, "very_large", false);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "413"}};

  assertCompareMaps<Http::ResponseHeaderMap>(expected_response_headers, response->headers());
  EXPECT_STREQ("Payload Too Large", response->body().c_str());

  // cleanup
  codec_client_->close();
  if (fake_upstream_connection_ != nullptr) {
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
}

struct WasmDeferredDataSetupFilterState {
  absl::Mutex mu;
  bool destroyed ABSL_GUARDED_BY(mu){false};
  bool decode_data_after_destroy ABSL_GUARDED_BY(mu){false};
  Http::StreamDecoderFilterCallbacks* callbacks ABSL_GUARDED_BY(mu){nullptr};
  Event::Dispatcher* dispatcher ABSL_GUARDED_BY(mu){nullptr};
  bool headers_processed ABSL_GUARDED_BY(mu){false};
};

class WasmDeferredDataSetupFilter : public Http::PassThroughFilter {
public:
  explicit WasmDeferredDataSetupFilter(std::shared_ptr<WasmDeferredDataSetupFilterState> state)
      : state_(std::move(state)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    bool first_request = false;
    {
      absl::MutexLock l(state_->mu);
      first_request = !state_->headers_processed;
    }
    // Only the first request triggers the read-disable that the bug reproduction
    // depends on. Follow-up requests pass through normally so they reach the
    // upstream and exercise the Wasm filter's reload path.
    if (!first_request) {
      return Http::FilterHeadersStatus::Continue;
    }
    decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    {
      absl::MutexLock l(state_->mu);
      state_->callbacks = decoder_callbacks_;
      state_->dispatcher = &decoder_callbacks_->dispatcher();
      state_->headers_processed = true;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    absl::MutexLock l(state_->mu);
    if (state_->destroyed) {
      state_->decode_data_after_destroy = true;
    }
    return Http::FilterDataStatus::Continue;
  }

  void onDestroy() override {
    absl::MutexLock l(state_->mu);
    state_->destroyed = true;
    state_->callbacks = nullptr;
  }

private:
  std::shared_ptr<WasmDeferredDataSetupFilterState> state_;
};

class WasmDeferredDataSetupFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  explicit WasmDeferredDataSetupFilterConfig(
      std::shared_ptr<WasmDeferredDataSetupFilterState> state)
      : EmptyHttpFilterConfig("setup-filter"), state_(std::move(state)) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [state = state_](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<WasmDeferredDataSetupFilter>(state));
    };
  }

private:
  std::shared_ptr<WasmDeferredDataSetupFilterState> state_;
};

class WasmDeferredDataTest : public HttpProtocolIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(IpVersions, WasmDeferredDataTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Verifies that a deferred data-processing callback does not invoke
// proxy_on_request_body on a Wasm filter whose context has already been
// destroyed (via proxy_on_delete) during connection close.
//
// Filter chain: [Wasm passthrough filter] -> [setup filter]
//
// Scenario:
//   1. Setup filter calls readDisable(true) in decodeHeaders.
//   2. Client sends DATA + END_STREAM -> codec buffers the frame.
//   3. A posted callback calls readDisable(false) (schedules
//      process_buffered_data_callback_) then closes the connection
//      (destroys filter chain -> Wasm context is deleted via proxy_on_delete).
//   4. Without the fix, the deferred process_buffered_data_callback_ fires,
//      calls decodeData on the Wasm filter with an already-deleted context_id,
//      causing a Rust panic "invalid context_id" that disables the Wasm VM.
TEST_P(WasmDeferredDataTest, InvalidContextIdPanicOnDeferredData) {
  // The bug is runtime-agnostic, but testing with v8 is sufficient.
  auto runtimes = Extensions::Common::Wasm::wasmTestMatrix(false, false);
  if (!absl::c_any_of(runtimes, [](const auto& p) { return std::get<0>(p) == "v8"; })) {
    GTEST_SKIP() << "v8 runtime not available";
  }

  auto state = std::make_shared<WasmDeferredDataSetupFilterState>();

  // Register the C++ setup filter.
  WasmDeferredDataSetupFilterConfig filter_config(state);
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered(
      filter_config);

  // Prepend the setup filter (will end up second in chain).
  config_helper_.prependFilter(R"EOF(
name: setup-filter
)EOF");

  // Prepend the Wasm filter (will end up first in chain — hit by deferred data).
  // Use FAIL_RELOAD so that any Wasm panic during the test (i.e. the bug
  // reproduction firing proxy_on_request_body on a destroyed context) puts the
  // VM into RuntimeError; the next request through the filter then triggers a
  // VM reload and bumps the wasm.<name>.vm_reload_* counters. With the fix in
  // place, no panic occurs and these counters stay at 0.
  const std::string wasm_filter = TestEnvironment::substitute(R"EOF(
name: envoy.filters.http.wasm
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm
  config:
    name: deferred_data_regression
    failure_policy: FAIL_RELOAD
    reload_config:
      backoff:
        base_interval: 0.01s
    vm_config:
      runtime: envoy.wasm.runtime.v8
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/filters/http/wasm/test_data/deferred_data_rust.wasm"
)EOF");
  config_helper_.prependFilter(wasm_filter);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send HEADERS only. The Wasm filter passes through; the setup filter
  // calls readDisable(true) and sets headers_processed.
  auto [request_encoder, response_decoder] = codec_client_->startRequest(default_request_headers_);

  // Wait until the server has processed decodeHeaders and readDisable is on.
  {
    absl::MutexLock l(state->mu);
    state->mu.Await(absl::Condition(&state->headers_processed));
  }

  // Record rx bytes before DATA so we can tell when it arrives.
  uint64_t bytes_before_data = 0;
  if (auto ctr = test_server_->counter("http.config_test.downstream_cx_rx_bytes_total")) {
    bytes_before_data = ctr->value();
  }

  // Send DATA + END_STREAM. Because readDisable is on, the codec buffers
  // the frame (body_buffered_ = true) instead of delivering to the filter chain.
  codec_client_->sendData(request_encoder, 1024, true);

  // Wait until the DATA frame (1024 payload + 9-byte frame header) has been
  // received by the server's HTTP/2 codec.
  test_server_->waitForCounter("http.config_test.downstream_cx_rx_bytes_total",
                               Ge(bytes_before_data + 1033));

  // Grab the worker-thread dispatcher and callbacks.
  Event::Dispatcher* conn_dispatcher;
  Http::StreamDecoderFilterCallbacks* cbs;
  {
    absl::MutexLock l(state->mu);
    conn_dispatcher = state->dispatcher;
    cbs = state->callbacks;
  }
  ASSERT_NE(conn_dispatcher, nullptr);
  ASSERT_NE(cbs, nullptr);

  // Post a callback to the connection's worker-thread dispatcher:
  //   (a) readDisable(false)  ->  schedules process_buffered_data_callback_
  //   (b) Close connection    ->  synchronously destroys filter chain
  // After the callback returns, the event loop fires the deferred
  // process_buffered_data_callback_.
  absl::Notification first_callback_done;
  conn_dispatcher->post([cbs, &first_callback_done]() {
    cbs->onDecoderFilterBelowWriteBufferLowWatermark();
    auto conn_ref = cbs->connection();
    if (conn_ref.has_value()) {
      const_cast<Network::Connection&>(conn_ref.ref())
          .close(Network::ConnectionCloseType::NoFlush, "wasm-deferred-data-test");
    }
    first_callback_done.Notify();
  });

  // Wait for the client to see the disconnect.
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  first_callback_done.WaitForNotification();

  // Post a fence callback: because process_buffered_data_callback_ was
  // scheduled via scheduleCallbackCurrentIteration() before this post(), the
  // event loop will fire it BEFORE this fence. Waiting guarantees that the
  // deferred decodeData (and any resulting Wasm panic) has already happened.
  absl::Notification event_loop_drained;
  conn_dispatcher->post([&event_loop_drained]() { event_loop_drained.Notify(); });
  event_loop_drained.WaitForNotification();

  // Check 1: the setup filter must not have seen decodeData after destroy.
  {
    absl::MutexLock l(state->mu);
    EXPECT_FALSE(state->decode_data_after_destroy)
        << "decodeData was invoked after onDestroy — deferred processing "
           "callback fired on the destroyed filter chain (use-after-free bug)";
  }

  // Check 2: send a normal follow-up request that traverses the Wasm filter on
  // the same worker thread. With the bug present, the Wasm VM was put into
  // RuntimeError by the deferred decodeData panic, and FAIL_RELOAD will trigger
  // a reload here — incrementing one of the vm_reload_* counters. With the fix,
  // the VM is healthy and no reload is attempted.
  const uint64_t initial_rq_total =
      test_server_->counter("http.config_test.downstream_rq_total")->value();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto follow_up_response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  test_server_->waitForCounter("http.config_test.downstream_rq_total", Ge(initial_rq_total + 1));
  // The reload check (when the bug is present) runs on the worker thread as
  // part of the Wasm filter's per-request setup; downstream_rq_total can tick
  // before that work has finished. Post a fence to the same worker dispatcher
  // and wait for it so any pending vm_reload_* increments have landed before
  // we read the counters below.
  absl::Notification follow_up_drained;
  conn_dispatcher->post([&follow_up_drained]() { follow_up_drained.Notify(); });
  follow_up_drained.WaitForNotification();
  codec_client_->close();

  constexpr absl::string_view stat_prefix = "wasm.deferred_data_regression.";
  EXPECT_EQ(0, test_server_->counter(absl::StrCat(stat_prefix, "vm_reload_success"))->value());
  EXPECT_EQ(0, test_server_->counter(absl::StrCat(stat_prefix, "vm_reload_backoff"))->value());
  EXPECT_EQ(0, test_server_->counter(absl::StrCat(stat_prefix, "vm_reload_failure"))->value());
}

} // namespace
} // namespace Wasm
} // namespace Extensions
} // namespace Envoy

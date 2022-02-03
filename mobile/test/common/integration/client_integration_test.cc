#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/http/common.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/integration.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/http/client.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

using testing::ReturnRef;

namespace Envoy {
namespace {

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  std::unique_ptr<Http::ResponseHeaderMapImpl> transformed_headers =
      Http::ResponseHeaderMapImpl::create();
  transformed_headers->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(false));
  Http::Utility::toEnvoyHeaders(*transformed_headers, headers);
  return transformed_headers;
}

typedef struct {
  uint32_t on_headers_calls;
  uint32_t on_data_calls;
  uint32_t on_complete_calls;
  uint32_t on_error_calls;
  uint32_t on_cancel_calls;
  uint64_t on_header_consumed_bytes_from_response;
  uint64_t on_complete_received_byte_count;
  std::string status;
  ConditionalInitializer* terminal_callback;
} callbacks_called;

void validateStreamIntel(const envoy_final_stream_intel& final_intel) {
  // This test doesn't do DNS lookup
  EXPECT_EQ(-1, final_intel.dns_start_ms);
  EXPECT_EQ(-1, final_intel.dns_end_ms);
  // This test doesn't do TLS.
  EXPECT_EQ(-1, final_intel.ssl_start_ms);
  EXPECT_EQ(-1, final_intel.ssl_end_ms);

  ASSERT_NE(-1, final_intel.request_start_ms);
  ASSERT_NE(-1, final_intel.connect_start_ms);
  ASSERT_NE(-1, final_intel.connect_end_ms);
  ASSERT_NE(-1, final_intel.sending_start_ms);
  ASSERT_NE(-1, final_intel.sending_end_ms);
  ASSERT_NE(-1, final_intel.response_start_ms);
  ASSERT_NE(-1, final_intel.request_end_ms);

  ASSERT_LE(final_intel.request_start_ms, final_intel.connect_start_ms);
  ASSERT_LE(final_intel.connect_start_ms, final_intel.connect_end_ms);
  ASSERT_LE(final_intel.connect_end_ms, final_intel.sending_start_ms);
  ASSERT_LE(final_intel.sending_start_ms, final_intel.sending_end_ms);
  ASSERT_LE(final_intel.request_end_ms, final_intel.response_start_ms);
  ASSERT_LE(final_intel.request_end_ms, final_intel.sending_end_ms);
}

// TODO(junr03): move this to derive from the ApiListenerIntegrationTest after moving that class
// into a test lib.
class ClientIntegrationTest : public BaseIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ClientIntegrationTest() : BaseIntegrationTest(GetParam(), bootstrap_config()) {
    use_lds_ = false;
    autonomous_upstream_ = true;
    defer_listener_finalization_ = true;
  }

  void SetUp() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // currently ApiListener does not trigger this wait
      // https://github.com/envoyproxy/envoy/blob/0b92c58d08d28ba7ef0ed5aaf44f90f0fccc5dce/test/integration/integration.cc#L454
      // Thus, the ApiListener has to be added in addition to the already existing listener in the
      // config.
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(
          Server::parseListenerFromV3Yaml(api_listener_config()));
    });

    bridge_callbacks_.context = &cc_;
    bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel intel,
                                      void* context) -> void* {
      Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
      callbacks_called* cc_ = static_cast<callbacks_called*>(context);
      cc_->on_headers_calls++;
      cc_->status = response_headers->Status()->value().getStringView();
      cc_->on_header_consumed_bytes_from_response = intel.consumed_bytes_from_response;
      return nullptr;
    };
    bridge_callbacks_.on_data = [](envoy_data c_data, bool, envoy_stream_intel,
                                   void* context) -> void* {
      callbacks_called* cc_ = static_cast<callbacks_called*>(context);
      cc_->on_data_calls++;
      release_envoy_data(c_data);
      return nullptr;
    };
    bridge_callbacks_.on_complete = [](envoy_stream_intel, envoy_final_stream_intel final_intel,
                                       void* context) -> void* {
      validateStreamIntel(final_intel);
      callbacks_called* cc_ = static_cast<callbacks_called*>(context);
      cc_->on_complete_received_byte_count = final_intel.received_byte_count;
      cc_->on_complete_calls++;
      cc_->terminal_callback->setReady();
      return nullptr;
    };
    bridge_callbacks_.on_error = [](envoy_error error, envoy_stream_intel, envoy_final_stream_intel,
                                    void* context) -> void* {
      release_envoy_error(error);
      callbacks_called* cc_ = static_cast<callbacks_called*>(context);
      cc_->on_error_calls++;
      cc_->terminal_callback->setReady();
      return nullptr;
    };
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  static std::string bootstrap_config() {
    // At least one empty filter chain needs to be specified.
    return ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      filters:
    )EOF";
  }

  static std::string api_listener_config() {
    return R"EOF(
name: api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
    config:
      stat_prefix: hcm
      route_config:
        virtual_hosts:
          name: integration
          routes:
            route:
              cluster: cluster_0
            match:
              prefix: "/"
          domains: "*"
        name: route_config_0
      http_filters:
        - name: envoy.filters.http.local_error
          typed_config:
            "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
        - name: envoy.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      )EOF";
  }

  Event::ProvisionalDispatcherPtr dispatcher_ = std::make_unique<Event::ProvisionalDispatcher>();
  Http::ClientPtr http_client_{};
  envoy_http_callbacks bridge_callbacks_;
  ConditionalInitializer terminal_callback_;
  callbacks_called cc_ = {0, 0, 0, 0, 0, 0, 0, "", &terminal_callback_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientIntegrationTest, Basic) {
  initialize();

  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  envoy_stream_t stream = 1;
  bridge_callbacks_.on_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                 void* context) -> void* {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_data_calls++;
    release_envoy_data(c_data);
    return nullptr;
  };

  // Build a set of request headers.
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  Http::TestRequestHeaderMapImpl headers{
      {AutonomousStream::EXPECT_REQUEST_SIZE_BYTES, std::to_string(request_data.length())}};

  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Build body data
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  // Build a set of request trailers.
  // TODO: update the autonomous upstream to assert on trailers, or to send trailers back.
  Http::TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Http::Utility::toBridgeHeaders(trailers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream, bridge_callbacks_, false);
    http_client_->sendHeaders(stream, c_headers, false);
    http_client_->sendData(stream, c_data, false);
    http_client_->sendTrailers(stream, c_trailers);
  });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);

  // stream_success gets charged for 2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_success", 1);
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  initialize();

  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}, {"content-length", "0"}})));

  envoy_stream_t stream = 1;
  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream, bridge_callbacks_, false);
    http_client_->sendHeaders(stream, c_headers, true);
  });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.status, "503");
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // stream_failure gets charged for all non-2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

TEST_P(ClientIntegrationTest, BasicReset) {
  initialize();

  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  envoy_stream_t stream = 1;

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  // Cause an upstream reset after request is complete.
  headers.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream, bridge_callbacks_, false);
    http_client_->sendHeaders(stream, c_headers, true);
  });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 1);
  ASSERT_EQ(cc_.on_headers_calls, 0);
  // Reset causes a charge to stream_failure.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

// TODO(junr03): test with envoy local reply with local stream not closed, which causes a reset
// fired from the Http:ConnectionManager rather than the Http::Client. This cannot be done in
// unit tests because the Http::ConnectionManager is mocked using a mock response encoder.

// Test header key case sensitivity.
TEST_P(ClientIntegrationTest, CaseSensitive) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto typed_extension_config = protocol_options.mutable_explicit_http_config()
                                      ->mutable_http_protocol_options()
                                      ->mutable_header_key_format()
                                      ->mutable_stateful_formatter();
    typed_extension_config->set_name("preserve_case");
    typed_extension_config->mutable_typed_config()->set_type_url(
        "type.googleapis.com/"
        "envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig");
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  autonomous_upstream_ = false;
  initialize();

  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  envoy_stream_t stream = 1;
  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    EXPECT_EQ("My-ResponsE-Header",
              response_headers->formatter().value().get().format("my-response-header"));
    return nullptr;
  };

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers{{"FoO", "bar"}};
  headers.header_map_->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(false));
  headers.header_map_->formatter().value().get().processKey("FoO");
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream, bridge_callbacks_, false);
    http_client_->sendHeaders(stream, c_headers, true);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  EXPECT_TRUE(absl::StrContains(upstream_request, "FoO: bar")) << upstream_request;

  // Verify that the downstream response has preserved cased headers.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // stream_success gets charged for 2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_success", 1);
}

} // namespace
} // namespace Envoy

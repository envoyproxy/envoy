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
  Http::ResponseHeaderMapPtr transformed_headers = Http::ResponseHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        Http::LowerCaseString(Data::Utility::copyToString(headers.entries[i].key)),
        Data::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

typedef struct {
  uint32_t on_headers_calls;
  uint32_t on_data_calls;
  uint32_t on_complete_calls;
  uint32_t on_error_calls;
  uint32_t on_cancel_calls;
  ConditionalInitializer* terminal_callback;
} callbacks_called;

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
    BaseIntegrationTest::initialize();
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
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
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

  std::atomic<envoy_network_t> preferred_network_{ENVOY_NET_GENERIC};
  Event::ProvisionalDispatcherPtr dispatcher_ = std::make_unique<Event::ProvisionalDispatcher>();
  Http::ClientPtr http_client_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientIntegrationTest, Basic) {
  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), preferred_network_,
        test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  envoy_stream_t stream = 1;
  ConditionalInitializer terminal_callback;
  // Setup bridge_callbacks to handle the response.
  envoy_http_callbacks bridge_callbacks;
  callbacks_called cc = {0, 0, 0, 0, 0, &terminal_callback};
  bridge_callbacks.context = &cc;
  bridge_callbacks.on_headers = [](envoy_headers c_headers, bool end_stream,
                                   void* context) -> void* {
    EXPECT_FALSE(end_stream);
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers_calls++;
    return nullptr;
  };
  bridge_callbacks.on_data = [](envoy_data c_data, bool end_stream, void* context) -> void* {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_data_calls++;
    c_data.release(c_data.context);
    return nullptr;
  };
  bridge_callbacks.on_complete = [](void* context) -> void* {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete_calls++;
    cc->terminal_callback->setReady();
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
    EXPECT_EQ(http_client_->startStream(stream, bridge_callbacks), ENVOY_SUCCESS);
    http_client_->sendHeaders(stream, c_headers, false);
    http_client_->sendData(stream, c_data, false);
    http_client_->sendTrailers(stream, c_trailers);
  });
  terminal_callback.waitReady();

  ASSERT_EQ(cc.on_headers_calls, 1);
  ASSERT_EQ(cc.on_data_calls, 2);
  ASSERT_EQ(cc.on_complete_calls, 1);

  // stream_success gets charged for 2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_success", 1);
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), preferred_network_,
        test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}, {"content-length", "0"}})));

  envoy_stream_t stream = 1;
  // Setup bridge_callbacks to handle the response.
  envoy_http_callbacks bridge_callbacks;
  ConditionalInitializer terminal_callback;
  callbacks_called cc = {0, 0, 0, 0, 0, &terminal_callback};
  bridge_callbacks.context = &cc;
  bridge_callbacks.on_headers = [](envoy_headers c_headers, bool end_stream,
                                   void* context) -> void* {
    EXPECT_TRUE(end_stream);
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "503");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers_calls++;
    return nullptr;
  };
  bridge_callbacks.on_complete = [](void* context) -> void* {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete_calls++;
    cc->terminal_callback->setReady();
    return nullptr;
  };
  bridge_callbacks.on_error = [](envoy_error error, void* context) -> void* {
    error.message.release(error.message.context);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error_calls++;
    return nullptr;
  };

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    EXPECT_EQ(http_client_->startStream(stream, bridge_callbacks), ENVOY_SUCCESS);
    http_client_->sendHeaders(stream, c_headers, true);
  });
  terminal_callback.waitReady();

  ASSERT_EQ(cc.on_error_calls, 0);
  ASSERT_EQ(cc.on_headers_calls, 1);
  ASSERT_EQ(cc.on_complete_calls, 1);

  // stream_failure gets charged for all non-2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

TEST_P(ClientIntegrationTest, BasicReset) {
  ConditionalInitializer server_started;
  test_server_->server().dispatcher().post([this, &server_started]() -> void {
    http_client_ = std::make_unique<Http::Client>(
        test_server_->server().listenerManager().apiListener()->get().http()->get(), *dispatcher_,
        test_server_->statStore(), preferred_network_,
        test_server_->server().api().randomGenerator());
    dispatcher_->drain(test_server_->server().dispatcher());
    server_started.setReady();
  });
  server_started.waitReady();

  envoy_stream_t stream = 1;
  // Setup bridge_callbacks to handle the response.
  envoy_http_callbacks bridge_callbacks;
  ConditionalInitializer terminal_callback;
  callbacks_called cc = {0, 0, 0, 0, 0, &terminal_callback};
  bridge_callbacks.context = &cc;
  bridge_callbacks.on_headers = [](envoy_headers c_headers, bool, void*) -> void* {
    release_envoy_headers(c_headers);
    ADD_FAILURE() << "unexpected call to on_headers";
    return nullptr;
  };
  bridge_callbacks.on_error = [](envoy_error error, void* context) -> void* {
    error.message.release(error.message.context);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error_calls++;
    cc->terminal_callback->setReady();
    return nullptr;
  };

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  // Cause an upstream reset after request is complete.
  headers.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    EXPECT_EQ(http_client_->startStream(stream, bridge_callbacks), ENVOY_SUCCESS);
    http_client_->sendHeaders(stream, c_headers, true);
  });
  terminal_callback.waitReady();

  ASSERT_EQ(cc.on_error_calls, 1);
  ASSERT_EQ(cc.on_headers_calls, 0);
  // Reset causes a charge to stream_failure.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

// TODO(junr03): test with envoy local reply with local stream not closed, which causes a reset
// fired from the Http:ConnectionManager rather than the Http::Client. This cannot be done in
// unit tests because the Http::ConnectionManager is mocked using a mock response encoder.

} // namespace
} // namespace Envoy

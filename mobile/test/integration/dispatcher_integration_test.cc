#include "test/common/http/common.h"
#include "test/integration/integration.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/dispatcher.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

using testing::ReturnRef;

namespace Envoy {
namespace {

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  Http::ResponseHeaderMapPtr transformed_headers = std::make_unique<Http::ResponseHeaderMapImpl>();
  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        Http::LowerCaseString(Http::Utility::convertToString(headers.headers[i].key)),
        Http::Utility::convertToString(headers.headers[i].value));
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
class DispatcherIntegrationTest : public BaseIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  DispatcherIntegrationTest() : BaseIntegrationTest(GetParam(), bootstrap_config()) {
    use_lds_ = false;
    autonomous_upstream_ = true;
  }

  void SetUp() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // currently ApiListener does not trigger this wait
      // https://github.com/envoyproxy/envoy/blob/0b92c58d08d28ba7ef0ed5aaf44f90f0fccc5dce/test/integration/integration.cc#L454
      // Thus, the ApiListener has to be added in addition to the already existing listener in the
      // config.
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(
          Server::parseListenerFromV2Yaml(api_listener_config()));
    });
    BaseIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  static std::string bootstrap_config() {
    // At least one empty filter chain needs to be specified.
    return ConfigHelper::BASE_CONFIG + R"EOF(
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
      - name: envoy.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      )EOF";
  }

  std::atomic<envoy_network_t> preferred_network_{ENVOY_NET_GENERIC};
  Http::Dispatcher http_dispatcher_{preferred_network_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DispatcherIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DispatcherIntegrationTest, Basic) {
  ConditionalInitializer ready_ran;
  test_server_->server().dispatcher().post([this, &ready_ran]() -> void {
    http_dispatcher_.ready(
        test_server_->server().dispatcher(),
        test_server_->server().listenerManager().apiListener()->get().http()->get());
    ready_ran.setReady();
  });
  ready_ran.waitReady();

  envoy_stream_t stream = 1;
  ConditionalInitializer terminal_callback;
  // Setup bridge_callbacks to handle the response.
  envoy_http_callbacks bridge_callbacks;
  callbacks_called cc = {0, 0, 0, 0, 0, &terminal_callback};
  bridge_callbacks.context = &cc;
  bridge_callbacks.on_headers = [](envoy_headers c_headers, bool end_stream,
                                   void* context) -> void {
    ASSERT_FALSE(end_stream);
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers_calls++;
  };
  bridge_callbacks.on_data = [](envoy_data c_data, bool end_stream, void* context) -> void {
    if (end_stream) {
      ASSERT_EQ(Http::Utility::convertToString(c_data), "");
    } else {
      ASSERT_EQ(c_data.length, 10);
    }
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_data_calls++;
    c_data.release(c_data.context);
  };
  bridge_callbacks.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete_calls++;
    cc->terminal_callback->setReady();
  };

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  EXPECT_EQ(http_dispatcher_.startStream(stream, bridge_callbacks), ENVOY_SUCCESS);
  http_dispatcher_.sendHeaders(stream, c_headers, true);

  terminal_callback.waitReady();

  ASSERT_EQ(cc.on_headers_calls, 1);
  ASSERT_EQ(cc.on_data_calls, 2);
  ASSERT_EQ(cc.on_complete_calls, 1);
}

TEST_P(DispatcherIntegrationTest, RaceDoesNotCauseDoubleDeletion) {
  ConditionalInitializer ready_ran;
  test_server_->server().dispatcher().post([this, &ready_ran]() -> void {
    http_dispatcher_.ready(
        test_server_->server().dispatcher(),
        test_server_->server().listenerManager().apiListener()->get().http()->get());
    ready_ran.setReady();
  });
  ready_ran.waitReady();

  http_dispatcher_.synchronizer().enable();

  envoy_stream_t stream = 1;
  // Setup bridge_callbacks to handle the response.
  envoy_http_callbacks bridge_callbacks;
  callbacks_called cc = {0, 0, 0, 0, 0, nullptr};
  bridge_callbacks.context = &cc;
  bridge_callbacks.on_headers = [](envoy_headers c_headers, bool end_stream,
                                   void* context) -> void {
    ASSERT_FALSE(end_stream);
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers_calls++;
  };
  bridge_callbacks.on_data = [](envoy_data c_data, bool end_stream, void* context) -> void {
    if (end_stream) {
      ASSERT_EQ(Http::Utility::convertToString(c_data), "");
    } else {
      ASSERT_EQ(c_data.length, 10);
    }
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_data_calls++;
    c_data.release(c_data.context);
  };
  bridge_callbacks.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete_calls++;
  };
  bridge_callbacks.on_cancel = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_cancel_calls++;
  };

  // Build a set of request headers.
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(headers);

  // Create a stream.
  EXPECT_EQ(http_dispatcher_.startStream(stream, bridge_callbacks), ENVOY_SUCCESS);
  http_dispatcher_.synchronizer().waitOn("dispatch_encode_final_data");
  http_dispatcher_.sendHeaders(stream, c_headers, true);
  http_dispatcher_.synchronizer().barrierOn("dispatch_encode_final_data");
  ASSERT_EQ(cc.on_headers_calls, 1);
  ASSERT_EQ(cc.on_data_calls, 1);
  ASSERT_EQ(cc.on_complete_calls, 0);

  ASSERT_EQ(http_dispatcher_.resetStream(stream), ENVOY_SUCCESS);
  ASSERT_EQ(cc.on_cancel_calls, 1);

  http_dispatcher_.synchronizer().signal("dispatch_encode_final_data");
}

} // namespace
} // namespace Envoy

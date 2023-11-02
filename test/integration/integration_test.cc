#include "test/integration/integration_test.h"

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/filters/process_context_filter.h"
#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using Envoy::Http::Headers;
using Envoy::Http::HeaderValueOf;
using Envoy::Http::HttpStatusIs;
using testing::Combine;
using testing::ContainsRegex;
using testing::EndsWith;
using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;
using testing::Values;
using testing::ValuesIn;

namespace Envoy {
namespace {

std::string normalizeDate(const std::string& s) {
  const std::regex date_regex("date:[^\r]+");
  return std::regex_replace(s, date_regex, "date: Mon, 01 Jan 2017 00:00:00 GMT");
}

void setDisallowAbsoluteUrl(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  hcm.mutable_http_protocol_options()->mutable_allow_absolute_url()->set_value(false);
};

void setAllowHttp10WithDefaultHost(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  hcm.mutable_http_protocol_options()->set_accept_http_10(true);
  hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
}

std::string testParamToString(
    const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Http1ParserImpl>>&
        params) {
  return absl::StrCat(TestUtility::ipVersionToString(std::get<0>(params.param)),
                      TestUtility::http1ParserImplToString(std::get<1>(params.param)));
}

} // namespace

INSTANTIATE_TEST_SUITE_P(IpVersionsAndHttp1Parser, IntegrationTest,
                         Combine(ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                 Values(Http1ParserImpl::HttpParser, Http1ParserImpl::BalsaParser)),
                         testParamToString);

// Verify that we gracefully handle an invalid pre-bind socket option when using reuse_port.
TEST_P(IntegrationTest, BadPrebindSocketOptionWithReusePort) {
  // Reserve a port that we can then use on the integration listener with reuse_port.
  auto addr_socket =
      Network::Test::bindFreeLoopbackPort(version_, Network::Socket::Type::Stream, true);
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_address()->mutable_socket_address()->set_port_value(
        addr_socket.second->connectionInfoProvider().localAddress()->ip()->port());
    auto socket_option = listener->add_socket_options();
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_PREBIND);
    socket_option->set_level(10000);     // Invalid level.
    socket_option->set_int_value(10000); // Invalid value.
  });
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Verify that we gracefully handle an invalid post-bind socket option when using reuse_port.
TEST_P(IntegrationTest, BadPostbindSocketOptionWithReusePort) {
  // Reserve a port that we can then use on the integration listener with reuse_port.
  auto addr_socket =
      Network::Test::bindFreeLoopbackPort(version_, Network::Socket::Type::Stream, true);
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_address()->mutable_socket_address()->set_port_value(
        addr_socket.second->connectionInfoProvider().localAddress()->ip()->port());
    auto socket_option = listener->add_socket_options();
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_BOUND);
    socket_option->set_level(10000);     // Invalid level.
    socket_option->set_int_value(10000); // Invalid value.
  });
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Verify that we gracefully handle an invalid post-listen socket option.
TEST_P(IntegrationTest, BadPostListenSocketOption) {
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto socket_option = listener->add_socket_options();
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_LISTENING);
    socket_option->set_level(10000);     // Invalid level.
    socket_option->set_int_value(10000); // Invalid value.
  });
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Make sure we have correctly specified per-worker performance stats.
TEST_P(IntegrationTest, PerWorkerStatsAndBalancing) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin stats
  concurrency_ = 2;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_connection_balance_config()->mutable_exact_balance();
  });
  initialize();

  // Per-worker listener stats.
  auto check_listener_stats = [this](uint64_t cx_active, uint64_t cx_total) {
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_1.downstream_cx_total", cx_total);
    } else {
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_1.downstream_cx_total", cx_total);
    }
  };
  check_listener_stats(0, 0);

  // Main thread admin listener stats.
  test_server_->waitForCounterExists("listener.admin.main_thread.downstream_cx_total");

  // Per-thread watchdog stats.
  test_server_->waitForCounterExists("server.main_thread.watchdog_miss");
  test_server_->waitForCounterExists("server.worker_0.watchdog_miss");
  test_server_->waitForCounterExists("server.worker_1.watchdog_miss");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  check_listener_stats(1, 1);

  codec_client_->close();
  codec_client2->close();
  check_listener_stats(0, 1);
}

class TestConnectionBalanceFactory : public Network::ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom empty config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
  Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::make_shared<Network::ExactConnectionBalancerImpl>();
  }
  std::string name() const override { return "envoy.network.connection_balance.test"; }
};

// Test extend balance.
TEST_P(IntegrationTest, ConnectionBalanceFactory) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin stats
  concurrency_ = 2;

  TestConnectionBalanceFactory factory;
  Registry::InjectFactory<Envoy::Network::ConnectionBalanceFactory> registered(factory);

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    TestConnectionBalanceFactory test_connection_balancer;
    Registry::InjectFactory<Envoy::Network::ConnectionBalanceFactory> inject_factory(
        test_connection_balancer);

    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    auto* connection_balance_config = listener->mutable_connection_balance_config();
    auto* extend_balance_config = connection_balance_config->mutable_extend_balance();
    extend_balance_config->set_name("envoy.network.connection_balance.test");
    extend_balance_config->mutable_typed_config()->set_type_url(
        "type.googleapis.com/google.protobuf.Struct");
  });

  initialize();

  auto check_listener_stats = [this](uint64_t cx_active, uint64_t cx_total) {
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_1.downstream_cx_total", cx_total);
    } else {
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_1.downstream_cx_total", cx_total);
    }
  };
  check_listener_stats(0, 0);

  // Main thread admin listener stats.
  test_server_->waitForCounterExists("listener.admin.main_thread.downstream_cx_total");

  // Per-thread watchdog stats.
  test_server_->waitForCounterExists("server.main_thread.watchdog_miss");
  test_server_->waitForCounterExists("server.worker_0.watchdog_miss");
  test_server_->waitForCounterExists("server.worker_1.watchdog_miss");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  check_listener_stats(1, 1);

  codec_client_->close();
  codec_client2->close();
  check_listener_stats(0, 1);
}

// On OSX this is flaky as we can end up with connection imbalance.
#if !defined(__APPLE__)
// Make sure all workers pick up connections
TEST_P(IntegrationTest, AllWorkersAreHandlingLoad) {
  concurrency_ = 2;
  initialize();

  std::string worker0_stat_name, worker1_stat_name;
  if (version_ == Network::Address::IpVersion::v4) {
    worker0_stat_name = "listener.127.0.0.1_0.worker_0.downstream_cx_total";
    worker1_stat_name = "listener.127.0.0.1_0.worker_1.downstream_cx_total";
  } else {
    worker0_stat_name = "listener.[__1]_0.worker_0.downstream_cx_total";
    worker1_stat_name = "listener.[__1]_0.worker_1.downstream_cx_total";
  }

  test_server_->waitForCounterEq(worker0_stat_name, 0);
  test_server_->waitForCounterEq(worker1_stat_name, 0);

  // We set the counters for the two workers to see how many connections each handles.
  uint64_t w0_ctr = 0;
  uint64_t w1_ctr = 0;
  constexpr int loops = 5;
  for (int i = 0; i < loops; i++) {
    constexpr int requests_per_loop = 4;
    std::array<IntegrationCodecClientPtr, requests_per_loop> connections;
    for (int j = 0; j < requests_per_loop; j++) {
      connections[j] = makeHttpConnection(lookupPort("http"));
    }

    auto worker0_ctr = test_server_->counter(worker0_stat_name);
    auto worker1_ctr = test_server_->counter(worker1_stat_name);
    auto target = w0_ctr + w1_ctr + requests_per_loop;
    while (test_server_->counter(worker0_stat_name)->value() +
               test_server_->counter(worker1_stat_name)->value() <
           target) {
      timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
    }
    w0_ctr = test_server_->counter(worker0_stat_name)->value();
    w1_ctr = test_server_->counter(worker1_stat_name)->value();
    for (int j = 0; j < requests_per_loop; j++) {
      connections[j]->close();
    }
  }

  EXPECT_TRUE(w0_ctr > 1);
  EXPECT_TRUE(w1_ctr > 1);
}
#endif

TEST_P(IntegrationTest, RouterDirectResponseWithBody) {
  const std::string body = "Response body";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", body);
  static const std::string domain("direct.example.com");
  static const std::string prefix("/");
  static const Http::Code status(Http::Code::OK);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("x-additional-header");
        header_value_option->mutable_header()->set_value("example-value");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-type");
        header_value_option->mutable_header()->set_value("text/html");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        // Add a wrong content-length.
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-length");
        header_value_option->mutable_header()->set_value("2000");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        auto* virtual_host = route_config->add_virtual_hosts();
        virtual_host->set_name(domain);
        virtual_host->add_domains(domain);
        virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
        virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
            static_cast<uint32_t>(status));
        virtual_host->mutable_routes(0)->mutable_direct_response()->mutable_body()->set_filename(
            file_path);
      });
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "direct.example.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("example-value", response->headers()
                                 .get(Envoy::Http::LowerCaseString("x-additional-header"))[0]
                                 ->value()
                                 .getStringView());
  EXPECT_EQ("text/html", response->headers().getContentTypeValue());
  // Verify content-length is correct.
  EXPECT_EQ(fmt::format("{}", body.size()), response->headers().getContentLengthValue());
  EXPECT_EQ(body, response->body());
}

TEST_P(IntegrationTest, RouterDirectResponseEmptyBody) {
  useAccessLog("%ROUTE_NAME%");
  static const std::string domain("direct.example.com");
  static const std::string prefix("/");
  static const Http::Code status(Http::Code::OK);
  static const std::string route_name("direct_response_route");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("x-additional-header");
        header_value_option->mutable_header()->set_value("example-value");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-type");
        header_value_option->mutable_header()->set_value("text/html");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        // Add a wrong content-length.
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-length");
        header_value_option->mutable_header()->set_value("2000");
        header_value_option->set_append_action(
            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
        auto* virtual_host = route_config->add_virtual_hosts();
        virtual_host->set_name(domain);
        virtual_host->add_domains(domain);
        virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
        virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
            static_cast<uint32_t>(status));
        virtual_host->mutable_routes(0)->set_name(route_name);
      });
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "direct.example.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("example-value", response->headers()
                                 .get(Envoy::Http::LowerCaseString("x-additional-header"))[0]
                                 ->value()
                                 .getStringView());
  // Content-type header is removed.
  EXPECT_EQ(nullptr, response->headers().ContentType());
  // Content-length header is correct.
  EXPECT_EQ("0", response->headers().getContentLengthValue());

  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr(route_name));
}

TEST_P(IntegrationTest, ConnectionCloseHeader) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(10); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":authority", "host"}, {"connection", "close"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(1000)));

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().Connection, "close"));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, LargeFlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(128 * 1024, 128 * 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyAndContentLengthNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, true);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyAndContentLengthNoBuffer) {
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, LargeFlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.setBufferLimits(128 * 1024, 128 * 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/9508
TEST_P(IntegrationTest, ResponseFramedByConnectionCloseWithReadLimits) {
  // Set a small buffer limit on the downstream in order to trigger a call to trigger readDisable on
  // the upstream when proxying the response. Upstream limit needs to be larger so that
  // RawBufferSocket::doRead reads the response body and detects the upstream close in the same call
  // stack.
  config_helper_.setBufferLimits(100000, 1);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // Disable chunk encoding to trigger framing by connection close.
  upstream_request_->http1StreamEncoderOptions().value().get().disableChunkEncoding();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_EQ(512, response->body().size());
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(IntegrationTest, EnvoyProxyingEarly1xxWithEncoderFilter) {
  testEnvoyProxying1xx(true, true);
}

TEST_P(IntegrationTest, EnvoyProxyingLate1xxWithEncoderFilter) {
  testEnvoyProxying1xx(false, true);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10923.
TEST_P(IntegrationTest, EnvoyProxying1xxWithDecodeDataPause) {
  config_helper_.prependFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
  )EOF");
  testEnvoyProxying1xx(true);
}

// Test the x-envoy-is-timeout-retry header is set to false for retries that are not
// initiated by timeouts.
TEST_P(IntegrationTest, RouterIsTimeoutRetryHeader) {
  auto host = config_helper_.createVirtualHost("example.com", "/test_retry");
  host.set_include_is_timeout_retry_header(true);
  config_helper_.addVirtualHost(host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test_retry"},
                                     {":scheme", "http"},
                                     {":authority", "example.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest();
  // Send a non-timeout failure response.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  // 5XX responses are retried.
  waitForNextUpstreamRequest();

  // The request did not fail due to a timeout, therefore we expect the x-envoy-is-timeout-retry
  // header to be false.
  EXPECT_EQ(upstream_request_->headers().getEnvoyIsTimeoutRetryValue(), "false");

  // Return 200 to the retry.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

// Verifies that we can construct a match tree with a filter, and that we are able to skip
// filter invocation through the match tree.
TEST_P(IntegrationTest, MatchingHttpFilterConstruction) {
  concurrency_ = 2;

  config_helper_.prependFilter(R"EOF(
name: matcher
typed_config:
  "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
  extension_config:
    name: set-response-code
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
      code: 403
  xds_matcher:
    matcher_tree:
      input:
        name: request-headers
        typed_config:
          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
          header_name: match-header
      exact_match_map:
        map:
          match:
            action:
              name: skip
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  initialize();

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), HttpStatusIs("403"));
    codec_client_->close();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
        {":authority", "host"}, {"match-header", "match"},   {"content-type", "application/grpc"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  }

  auto second_codec = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"},   {":scheme", "http"},
      {":authority", "host"}, {"match-header", "not-match"}, {"content-type", "application/grpc"}};
  auto response = second_codec->makeRequestWithBody(request_headers, 1024);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));

  codec_client_->close();
  second_codec->close();
}

// Verifies that we can construct a match tree with a filter using the new matcher tree proto, and
// that we are able to skip filter invocation through the match tree.
TEST_P(IntegrationTest, MatchingHttpFilterConstructionNewProto) {
  concurrency_ = 2;

  config_helper_.prependFilter(R"EOF(
name: matcher
typed_config:
  "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
  extension_config:
    name: set-response-code
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
      code: 403
  xds_matcher:
    matcher_tree:
      input:
        name: request-headers
        typed_config:
          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
          header_name: match-header
      exact_match_map:
        map:
          match:
            action:
              name: skip
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  initialize();

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), HttpStatusIs("403"));
    codec_client_->close();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
        {":authority", "host"}, {"match-header", "match"},   {"content-type", "application/grpc"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  }

  auto second_codec = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"},   {":scheme", "http"},
      {":authority", "host"}, {"match-header", "not-match"}, {"content-type", "application/grpc"}};
  auto response = second_codec->makeRequestWithBody(request_headers, 1024);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));

  codec_client_->close();
  second_codec->close();
}

// Verifies routing via the match tree API.
TEST_P(IntegrationTest, MatchTreeRouting) {
  const std::string vhost_yaml = R"EOF(
    name: vhost
    domains: ["matcher.com"]
    matcher:
      matcher_tree:
        input:
          name: request-headers
          typed_config:
            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: match-header
        exact_match_map:
          map:
            "route":
              action:
                name: route
                typed_config:
                  "@type": type.googleapis.com/envoy.config.route.v3.Route
                  match:
                    prefix: /
                  route:
                    cluster: cluster_0
  )EOF";

  envoy::config::route::v3::VirtualHost virtual_host;
  TestUtility::loadFromYaml(vhost_yaml, virtual_host);

  config_helper_.addVirtualHost(virtual_host);
  autonomous_upstream_ = true;

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/whatever"},
                                         {":scheme", "http"},
                                         {"match-header", "route"},
                                         {":authority", "matcher.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));

  codec_client_->close();
}

// Verifies routing via the match tree API with prefix matching.
TEST_P(IntegrationTest, PrefixMatchTreeRouting) {
  const std::string vhost_yaml = R"EOF(
    name: vhost
    domains: ["matcher.com"]
    matcher:
      matcher_tree:
        input:
          name: request-headers
          typed_config:
            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: match-header
        prefix_match_map:
          map:
            "r":
              action:
                name: route
                typed_config:
                  "@type": type.googleapis.com/envoy.config.route.v3.Route
                  match:
                    prefix: /
                  route:
                    cluster: cluster_0
  )EOF";

  envoy::config::route::v3::VirtualHost virtual_host;
  TestUtility::loadFromYaml(vhost_yaml, virtual_host);

  config_helper_.addVirtualHost(virtual_host);
  autonomous_upstream_ = true;

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/whatever"},
                                         {":scheme", "http"},
                                         {"match-header", "route"},
                                         {":authority", "matcher.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));

  codec_client_->close();
}

// This is a regression for https://github.com/envoyproxy/envoy/issues/2715 and validates that a
// pending request is not sent on a connection that has been half-closed.
TEST_P(IntegrationTest, UpstreamDisconnectWithTwoRequests) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    // Ensure we only have one connection upstream, one request active at a time.
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_common_http_protocol_options()
        ->mutable_max_requests_per_connection()
        ->set_value(1);
    protocol_options.mutable_use_downstream_protocol_config();
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_connections()->set_value(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // Request 2.
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client2->makeRequestWithBody(default_request_headers_, 512);

  // Validate one request active, the other pending.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 1);

  // Response 1.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Response 2.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);
  ASSERT_TRUE(response2->waitForEndStream());
  codec_client2->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 2);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);
}

TEST_P(IntegrationTest, TestSmuggling) {
#ifdef ENVOY_ENABLE_UHV
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.disableDelayClose();
  initialize();

  // Make sure the http parser rejects having content-length and transfer-encoding: chunked
  // on the same request, regardless of order and spacing.
  {
    std::string response;
    const std::string full_request = "GET / HTTP/1.1\r\n"
                                     "Host: host\r\ncontent-length: 0\r\n"
                                     "transfer-encoding: chunked\r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
    EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  }

  // Check with a non-zero content length as well.
  {
    std::string response;
    const std::string full_request = "GET / HTTP/1.1\r\n"
                                     "Host: host\r\ncontent-length: 36\r\n"
                                     "transfer-encoding: chunked\r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
    EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  }

  // Make sure transfer encoding is still treated as such with leading whitespace.
  {
    std::string response;
    const std::string full_request = "GET / HTTP/1.1\r\n"
                                     "Host: host\r\ncontent-length: 0\r\n"
                                     "\ttransfer-encoding: chunked\r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
    EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  }

  {
    std::string response;
    const std::string request = "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: chunked "
                                "\r\ncontent-length: 36\r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
    EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  }
  {
    std::string response;
    const std::string request = "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: "
                                "identity,chunked \r\ncontent-length: 36\r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
    EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  }
  {
    // Verify that sending `Transfer-Encoding: chunked` as a second header is detected and triggers
    // the "no Transfer-Encoding + Content-Length" check.
    std::string response;
    const std::string request =
        "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: "
        "identity\r\ncontent-length: 36\r\ntransfer-encoding: chunked \r\n\r\n";
    sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
    EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  }
}

TEST_P(IntegrationTest, TestInvalidTransferEncoding) {
#ifdef ENVOY_ENABLE_UHV
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.disableDelayClose();
  initialize();

  // Verify that sending `Transfer-Encoding: chunked` as a second header is detected and triggers
  // the "bad Transfer-Encoding" check.
  std::string response;
  const std::string request = "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: "
                              "identity\r\ntransfer-encoding: chunked \r\n\r\n";
  sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 501 Not Implemented\r\n"));
}

TEST_P(IntegrationTest, TestPipelinedResponses) {
  initialize();
  auto tcp_client = makeTcpConnection(lookupPort("http"));

  ASSERT_TRUE(tcp_client->write(
      "POST /test/long/url HTTP/1.1\r\nHost: host\r\ntransfer-encoding: chunked\r\n\r\n"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  ASSERT_THAT(data, StartsWith("POST"));

  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n"
      "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n"
      "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n"));

  tcp_client->waitForData("0\r\n\r\n", false);
  std::string response = tcp_client->data();

  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("transfer-encoding: chunked\r\n"));
  EXPECT_THAT(response, EndsWith("0\r\n\r\n"));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_protocol_error")->value(), 1);
}

TEST_P(IntegrationTest, TestServerAllowChunkedLength) {
#ifdef ENVOY_ENABLE_UHV
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http_protocol_options()->set_allow_chunked_length(true);
      });
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("POST / HTTP/1.1\r\n"
                                "Host: host\r\n"
                                "Content-length: 100\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "4\r\nbody\r\n"
                                "0\r\n\r\n"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));

  ASSERT_THAT(data, StartsWith("POST / HTTP/1.1"));
  ASSERT_THAT(data, HasSubstr("transfer-encoding: chunked"));
  // verify no 'content-length' header
  ASSERT_THAT(data, Not(HasSubstr("ontent-length")));

  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(IntegrationTest, TestClientAllowChunkedLength) {
#ifdef ENVOY_ENABLE_UHV
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()
          ->mutable_http_protocol_options()
          ->set_allow_chunked_length(true);
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    }
  });

  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("GET / HTTP/1.1\r\nHost: host\r\n\r\n"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));

  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n"
                                              "Transfer-encoding: chunked\r\n"
                                              "Content-Length: 100\r\n\r\n"
                                              "4\r\nbody\r\n"
                                              "0\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  std::string response = tcp_client->data();

  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("content-length")));
  EXPECT_THAT(response, HasSubstr("transfer-encoding: chunked\r\n"));
  EXPECT_THAT(response, EndsWith("\r\n\r\n"));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(IntegrationTest, BadFirstline) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "hello\r\n", &response);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, MissingDelimiter) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\nHost: host\r\nfoo bar\r\n\r\n", &response);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("http1.codec_error"));
  EXPECT_THAT(log, HasSubstr("DPE"));
  EXPECT_THAT(log, Not(HasSubstr("DC")));
}

TEST_P(IntegrationTest, InvalidCharacterInFirstline) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GE(T / HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, InvalidVersion) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.01\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

// Expect that malformed trailers to break the connection
TEST_P(IntegrationTest, BadTrailer) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "POST / HTTP/1.1\r\n"
                                "Host: host\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "4\r\n"
                                "body\r\n0\r\n"
                                "badtrailer\r\n\r\n",
                                &response);

  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

// Expect malformed headers to break the connection
TEST_P(IntegrationTest, BadHeader) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "POST / HTTP/1.1\r\n"
                                "Host: host\r\n"
                                "badHeader\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "4\r\n"
                                "body\r\n0\r\n\r\n",
                                &response);

  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, Http10Disabled) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 426 Upgrade Required\r\n"));
}

TEST_P(IntegrationTest, Http10DisabledWithUpgrade) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\nUpgrade: h2c\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 426 Upgrade Required\r\n"));
}

// Turn HTTP/1.0 support on and verify 09 style requests work.
TEST_P(IntegrationTest, Http09Enabled) {
  useAccessLog();
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET /\r\n\r\n", &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("HTTP/1.0"));
}

TEST_P(IntegrationTest, Http09WithKeepalive) {
  if (http1_implementation_ == Http1ParserImpl::BalsaParser) {
    // HTTP/0.9 does not allow for headers.
    // BalsaParser correctly ignores data after "\r\n".
    return;
  }

  useAccessLog();
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "0"}})));
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET /\r\nConnection: keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: keep-alive\r\n"));
}

// Turn HTTP/1.0 support on and verify the request is proxied and the default host is sent upstream.
TEST_P(IntegrationTest, Http10Enabled) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");

  sendRawHttpAndWaitForResponse(lookupPort("http"), "HEAD / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));
}

TEST_P(IntegrationTest, SendFullyQualifiedUrl) {
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http_protocol_options()
        ->set_send_fully_qualified_url(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("GET / HTTP/1.1\r\nHost: host\r\n\r\n"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  EXPECT_TRUE(absl::StrContains(data, "http://host/"));

  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  std::string response = tcp_client->data();

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(IntegrationTest, TestInlineHeaders) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\n"
                                "Host: foo.com\r\n"
                                "Foo: bar\r\n"
                                "User-Agent: public\r\n"
                                "User-Agent: 123\r\n"
                                "Eep: baz\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
  EXPECT_EQ(upstream_headers->get_("User-Agent"), "public,123");
  ASSERT_FALSE(upstream_headers->get(Envoy::Http::LowerCaseString("foo")).empty());
  EXPECT_EQ("bar",
            upstream_headers->get(Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView());
  ASSERT_FALSE(upstream_headers->get(Envoy::Http::LowerCaseString("eep")).empty());
  EXPECT_EQ("baz",
            upstream_headers->get(Envoy::Http::LowerCaseString("eep"))[0]->value().getStringView());
}

// Verify for HTTP/1.0 a keep-alive header results in no connection: close.
// Also verify existing host headers are passed through for the HTTP/1.0 case.
// This also regression tests proper handling of trailing whitespace after key
// values, specifically the host header.
TEST_P(IntegrationTest, Http10WithHostandKeepAliveAndLwsNoContentLength) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.0\r\nHost: foo.com \r\nConnection:Keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("connection: keep-alive")));
  EXPECT_THAT(response, Not(HasSubstr("content-length:")));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
}

TEST_P(IntegrationTest, Http10WithHostandKeepAliveAndContentLengthAndLws) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "10"}})));
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.0\r\nHost: foo.com \r\nConnection:Keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("connection: close")));
  EXPECT_THAT(response, HasSubstr("connection: keep-alive"));
  EXPECT_THAT(response, HasSubstr("content-length:"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));
}

TEST_P(IntegrationTest, Pipeline) {
  autonomous_upstream_ = true;
  initialize();
  std::string response;

  auto connection = createConnectionDriver(
      lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\n\r\nGET / HTTP/1.1\r\n\r\n",
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });
  // First response should be success.
  while (response.find("200") == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));

  // Second response should be 400 (no host)
  while (response.find("400") == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  connection->close();
}

// Checks to ensure that we reject the third request that is pipelined in the
// same request
TEST_P(IntegrationTest, PipelineWithTrailers) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();
  std::string response;

  std::string good_request("POST / HTTP/1.1\r\n"
                           "Host: host\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "4\r\n"
                           "body\r\n0\r\n"
                           "trailer1:t2\r\n"
                           "trailer2:t3\r\n"
                           "\r\n");

  std::string bad_request("POST / HTTP/1.1\r\n"
                          "Host: host\r\n"
                          "Transfer-Encoding: chunked\r\n\r\n"
                          "4\r\n"
                          "body\r\n0\r\n"
                          "trailer1\r\n"
                          "trailer2:t3\r\n"
                          "\r\n");

  auto connection = createConnectionDriver(
      lookupPort("http"), absl::StrCat(good_request, good_request, bad_request),
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });

  // First response should be success.
  size_t pos;
  while ((pos = response.find("200")) == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));
  while (response.find("200", pos + 1) == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  while (response.find("400") == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }

  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  connection->close();
}

// Add a pipeline test where complete request headers in the first request merit
// an inline sendLocalReply to make sure the "kick" works under the call stack
// of dispatch as well as when a response is proxied from upstream.
TEST_P(IntegrationTest, PipelineInline) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_stream_error_on_invalid_http_message()->set_value(true); });

  autonomous_upstream_ = true;
  initialize();
  std::string response;

  auto connection = createConnectionDriver(
      lookupPort("http"), "GET / HTTP/1.1\r\n\r\nGET / HTTP/1.0\r\n\r\n",
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });

  while (response.find("400") == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));

  while (response.find("426") == std::string::npos) {
    ASSERT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 426 Upgrade Required\r\n"));
  connection->close();
}

TEST_P(IntegrationTest, NoHost) {
  disable_client_header_validation_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(IntegrationTest, BadPath) {
  config_helper_.addConfigModifier(&setDisallowAbsoluteUrl);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n", &response,
                                true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 404 Not Found\r\n"));
}

TEST_P(IntegrationTest, AbsolutePath) {
  // Configure www.redirect.com to send a redirect, and ensure the redirect is
  // encountered via absolute URL.
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
}

TEST_P(IntegrationTest, UnknownSchemeRejected) {
  // Sent an HTTPS request over non-TLS. It should be rejected.
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET hps://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, AbsolutePathUsingHttpsDisallowedAtFrontline) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_use_remote_address()->set_value(true); });
  // Sent an HTTPS request over non-TLS. It should be rejected.
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET https://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 403 Forbidden\r\n"));
}

TEST_P(IntegrationTest, AbsolutePathUsingHttpsAllowedInternally) {
  autonomous_upstream_ = true;
  // Sent an HTTPS request over non-TLS. It will be allowed for non-front-line Envoys
  // and match the configured redirect.
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET https://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
}

// Make that both IPv4 and IPv6 hosts match when using relative and absolute URLs.
TEST_P(IntegrationTest, TestHostWithAddress) {
  useAccessLog("%REQ(Host)%");
  std::string address_string;
  if (version_ == Network::Address::IpVersion::v4) {
    address_string = TestUtility::getIpv4Loopback();
  } else {
    address_string = "[::1]";
  }

  auto host = config_helper_.createVirtualHost(address_string.c_str(), "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;

  // Test absolute URL with ipv6.
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), absl::StrCat("GET http://", address_string, " HTTP/1.1\r\n\r\n").c_str(),
      &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr(address_string));

  // Test normal IPv6 request as well.
  response.clear();
  sendRawHttpAndWaitForResponse(
      lookupPort("http"),
      absl::StrCat("GET / HTTP/1.1\r\nHost: ", address_string, "\r\n\r\n").c_str(), &response,
      true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
}

TEST_P(IntegrationTest, AbsolutePathWithPort) {
  // Configure www.namewithport.com:1234 to send a redirect, and ensure the redirect is
  // encountered via absolute URL with a port.
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET http://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n",
      &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
}

TEST_P(IntegrationTest, AbsolutePathWithMixedScheme) {
  // Configure www.namewithport.com:1234 to send a redirect, and ensure the redirect is
  // encountered via absolute URL with a port.
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET hTtp://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n",
      &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 301"));
}

TEST_P(IntegrationTest, AbsolutePathWithMixedSchemeLegacy) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_absolute_url_with_mixed_scheme", "false");
  config_helper_.addRuntimeOverride("envoy.reloadable_features.handle_uppercase_scheme", "false");

  // Mixed scheme requests will be rejected
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET hTtp://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n",
      &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400"));
}

TEST_P(IntegrationTest, AbsolutePathWithoutPort) {
  // Add a restrictive default match, to avoid the request hitting the * / catchall.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  // Set a matcher for www.namewithport.com:1234 and verify http://www.namewithport.com does not
  // match
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.namewithport.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 404 Not Found\r\n"));
}

// Ensure that connect behaves the same with allow_absolute_url enabled and without
TEST_P(IntegrationTest, Connect) {
  setListenersBoundTimeout(3 * TestUtility::DefaultTimeout);
  const std::string& request = "CONNECT www.somewhere.com:80 HTTP/1.1\r\n\r\n";
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // Clone the whole listener.
    auto static_resources = bootstrap.mutable_static_resources();
    auto* old_listener = static_resources->mutable_listeners(0);
    auto* cloned_listener = static_resources->add_listeners();
    cloned_listener->CopyFrom(*old_listener);
    old_listener->set_name("http_forward");
  });
  // Set the first listener to disallow absolute URLs.
  config_helper_.addConfigModifier(&setDisallowAbsoluteUrl);
  initialize();

  std::string response1;
  sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response1, true);

  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http_forward"), request.c_str(), &response2, true);

  EXPECT_EQ(normalizeDate(response1), normalizeDate(response2));
}

// Test that Envoy returns HTTP code 502 on upstream protocol error.
TEST_P(IntegrationTest, UpstreamProtocolError) { testRouterUpstreamProtocolError("502", "UPE"); }

TEST_P(IntegrationTest, TestHead) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl head_request{{":method", "HEAD"},
                                              {":path", "/test/long/url"},
                                              {":scheme", "http"},
                                              {":authority", "host"}};

  // Without an explicit content length, assume we chunk for HTTP/1.1
  auto response = sendRequestAndWaitForResponse(head_request, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_EQ(response->headers().ContentLength(), nullptr);
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Headers::get().TransferEncoding,
                            Http::Headers::get().TransferEncodingValues.Chunked));
  EXPECT_EQ(0, response->body().size());

  // Preserve explicit content length.
  Http::TestResponseHeaderMapImpl content_length_response{{":status", "200"},
                                                          {"content-length", "12"}};
  response = sendRequestAndWaitForResponse(head_request, 0, content_length_response, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().ContentLength, "12"));
  EXPECT_EQ(response->headers().TransferEncoding(), nullptr);
  EXPECT_EQ(0, response->body().size());
}

// The Envoy HTTP/1.1 codec ASSERTs that T-E headers are cleared in
// encodeHeaders, so to test upstreams explicitly sending T-E: chunked we have
// to send raw HTTP.
TEST_P(IntegrationTest, TestHeadWithExplicitTE) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("HEAD / HTTP/1.1\r\nHost: host\r\n\r\n"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));

  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  std::string response = tcp_client->data();

  EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("content-length")));
  EXPECT_THAT(response, HasSubstr("transfer-encoding: chunked\r\n"));
  EXPECT_THAT(response, EndsWith("\r\n\r\n"));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(IntegrationTest, TestBind) {
  std::string address_string;
  if (version_ == Network::Address::IpVersion::v4) {
    address_string = TestUtility::getIpv4Loopback();
  } else {
    address_string = "::1";
  }
  config_helper_.setSourceAddress(address_string);
  useAccessLog("%UPSTREAM_LOCAL_ADDRESS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                         1024);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_NE(fake_upstream_connection_, nullptr);
  std::string address = fake_upstream_connection_->connection()
                            .connectionInfoProvider()
                            .remoteAddress()
                            ->ip()
                            ->addressAsString();
  EXPECT_EQ(address, address_string);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_NE(upstream_request_, nullptr);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  cleanupUpstreamAndDownstream();
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr(address_string));
}

TEST_P(IntegrationTest, TestFailedBind) {
  config_helper_.setSourceAddress("8.8.8.8");
  config_helper_.addConfigModifier(configureProxyStatus());

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // With no ability to successfully bind on an upstream connection Envoy should
  // send a 500.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-upstream-rq-timeout-ms", "1000"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("503"));
  EXPECT_THAT(
      response->headers().getProxyStatusValue(),
      ContainsRegex(
          R"(envoy; error=connection_refused; details="upstream_reset_before_response_started\{.*\}; UF")"));
  EXPECT_LT(0, test_server_->counter("cluster.cluster_0.bind_errors")->value());
}

ConfigHelper::HttpModifierFunction setVia(const std::string& via) {
  return
      [via](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.set_via(via); };
}

// Validate in a basic header-only request we get via header insertion.
TEST_P(IntegrationTest, ViaAppendHeaderOnly) {
  config_helper_.addConfigModifier(setVia("bar"));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"via", "foo"},
                                     {"connection", "close"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().Via, "foo, bar"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().Via, "bar"));
}

// Validate that 100-continue works as expected with via header addition on both request and
// response path.
TEST_P(IntegrationTest, ViaAppendWith1xx) {
  config_helper_.addConfigModifier(setVia("foo"));
  testEnvoyHandling1xx(false, "foo");
}

// Test delayed close semantics for downstream HTTP/1.1 connections. When an early response is
// sent by Envoy, it will wait for response acknowledgment (via FIN/RST) from the client before
// closing the socket (with a timeout for ensuring cleanup).
TEST_P(IntegrationTest, TestDelayedConnectionTeardownOnGracefulClose) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
  // This test will trigger an early 413 Payload Too Large response due to buffer limits being
  // exceeded. The following filter is needed since the router filter will never trigger a 413.
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  // With no delayed close processing, Envoy will close the connection immediately after flushing
  // and this should instead return true.
  EXPECT_FALSE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));

  // Issue a local close and check that the client did not pick up a remote close which can happen
  // when delayed close semantics are disabled.
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::LocalClose);
}

// Test configuration of the delayed close timeout on downstream HTTP/1.1 connections. A value of 0
// disables delayed close processing.
TEST_P(IntegrationTest, TestDelayedConnectionTeardownConfig) {
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(0); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  // There is a potential race in the client's response processing when delayed close logic is
  // disabled in Envoy (see https://github.com/envoyproxy/envoy/issues/2929). Depending on timing,
  // a client may receive an RST prior to reading the response data from the socket, which may clear
  // the receive buffers. Also, clients which don't flush the receive buffer upon receiving a remote
  // close may also lose data (Envoy is susceptible to this).
  // Therefore, avoid checking response code/payload here and instead simply look for the remote
  // close.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
}

// Test that if the route cache is cleared, it doesn't cause problems.
TEST_P(IntegrationTest, TestClearingRouteCacheFilter) {
  config_helper_.prependFilter("{ name: clear-route-cache }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
}

// Test that if no connection pools are free, Envoy fails to establish an upstream connection.
TEST_P(IntegrationTest, NoConnectionPoolsFree) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    // Somewhat contrived with 0, but this is the simplest way to test right now.
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_connection_pools()->set_value(0);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);

  // Validate none active.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 0);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_503", 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_pool_overflow")->value(), 1);
}

TEST_P(IntegrationTest, ProcessObjectHealthy) {
  config_helper_.prependFilter("{ name: process-context-filter }");

  ProcessObjectForFilter healthy_object(true);
  process_object_ = healthy_object;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":authority", "host"},
                                                                          {"connection", "close"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

TEST_P(IntegrationTest, ProcessObjectUnealthy) {
  config_helper_.prependFilter("{ name: process-context-filter }");

  ProcessObjectForFilter unhealthy_object(false);
  process_object_ = unhealthy_object;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":authority", "host"},
                                                                          {"connection", "close"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("500"));
}

TEST_P(IntegrationTest, TrailersDroppedDuringEncoding) { testTrailers(10, 10, false, false); }

TEST_P(IntegrationTest, TrailersDroppedUpstream) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  testTrailers(10, 10, false, false);
}

TEST_P(IntegrationTest, TrailersDroppedDownstream) {
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  testTrailers(10, 10, false, false);
}

INSTANTIATE_TEST_SUITE_P(IpVersionsAndHttp1Parser, UpstreamEndpointIntegrationTest,
                         Combine(ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                 Values(Http1ParserImpl::HttpParser, Http1ParserImpl::BalsaParser)),
                         testParamToString);

TEST_P(UpstreamEndpointIntegrationTest, TestUpstreamEndpointAddress) {
  initialize();
  EXPECT_STREQ(fake_upstreams_[0]->localAddress()->ip()->addressAsString().c_str(),
               Network::Test::getLoopbackAddressString(version_).c_str());
}

// Send continuous pipelined requests while not reading responses, to check
// HTTP/1.1 response flood protection.
TEST_P(IntegrationTest, TestFlood) {
  config_helper_.setListenerSendBufLimits(1024);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(true);
      });
  initialize();

  // Set up a raw connection to easily send requests without reading responses.
  Network::ClientConnectionPtr raw_connection = makeClientConnection(lookupPort("http"));
  raw_connection->connect();

  // Read disable so responses will queue up.
  uint32_t bytes_to_send = 0;
  raw_connection->readDisable(true);
  // Track locally queued bytes, to make sure the outbound client queue doesn't back up.
  raw_connection->addBytesSentCallback([&](uint64_t bytes) {
    bytes_to_send -= bytes;
    return true;
  });

  // Keep sending requests until flood protection kicks in and kills the connection.
  while (raw_connection->state() == Network::Connection::State::Open) {
    // These requests are missing the host header, so will provoke an internally generated error
    // response from Envoy.
    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n");
    bytes_to_send += buffer.length();
    raw_connection->write(buffer, false);
    // Loop until all bytes are sent.
    while (bytes_to_send > 0 && raw_connection->state() == Network::Connection::State::Open) {
      raw_connection->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Verify the connection was closed due to flood protection.
  EXPECT_EQ(1, test_server_->counter("http1.response_flood")->value());
}

TEST_P(IntegrationTest, TestFloodUpstreamErrors) {
  config_helper_.setListenerSendBufLimits(1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
  autonomous_upstream_ = true;
  initialize();

  // Set an Upstream reply with an invalid content-length, which will be rejected by the Envoy.
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "invalid"}}));
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::move(response_headers));

  // Set up a raw connection to easily send requests without reading responses. Also, set a small
  // TCP receive buffer to speed up connection backup while proxying the response flood.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));
  Network::ClientConnectionPtr raw_connection =
      makeClientConnectionWithOptions(lookupPort("http"), options);
  raw_connection->connect();

  // Read disable so responses will queue up.
  uint32_t bytes_to_send = 0;
  raw_connection->readDisable(true);
  // Track locally queued bytes, to make sure the outbound client queue doesn't back up.
  raw_connection->addBytesSentCallback([&](uint64_t bytes) {
    bytes_to_send -= bytes;
    return true;
  });

  // Keep sending requests until flood protection kicks in and kills the connection.
  while (raw_connection->state() == Network::Connection::State::Open) {
    // The upstream response is invalid, and will trigger an internally generated error response
    // from Envoy.
    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nhost: foo.com\r\n\r\n");
    bytes_to_send += buffer.length();
    raw_connection->write(buffer, false);
    // Loop until all bytes are sent.
    while (bytes_to_send > 0 && raw_connection->state() == Network::Connection::State::Open) {
      raw_connection->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Verify the connection was closed due to flood protection.
  EXPECT_EQ(1, test_server_->counter("http1.response_flood")->value());
}

// Make sure flood protection doesn't kick in with many requests sent serially.
TEST_P(IntegrationTest, TestManyBadRequests) {
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(true);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl bad_request{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}};

  for (int i = 0; i < 1000; ++i) {
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(bad_request);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), HttpStatusIs("400"));
  }
  EXPECT_EQ(0, test_server_->counter("http1.response_flood")->value());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10566
TEST_P(IntegrationTest, TestUpgradeHeaderInResponse) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n"
                                              "connection: upgrade\r\n"
                                              "upgrade: h2\r\n"
                                              "Transfer-encoding: chunked\r\n\r\n"
                                              "b\r\nHello World\r\n0\r\n\r\n",
                                              false));

  response->waitForHeaders();
  EXPECT_EQ(nullptr, response->headers().Upgrade());
  EXPECT_EQ(nullptr, response->headers().Connection());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("Hello World", response->body());
}

// Expect that if an upgrade was not expected, the HCM correctly removes upgrade headers from the
// response and the response encoder does not drop trailers.
TEST_P(IntegrationTest, TestUpgradeHeaderInResponseWithTrailers) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n"
                                              "connection: upgrade\r\n"
                                              "upgrade: websocket\r\n"
                                              "Transfer-encoding: chunked\r\n\r\n"
                                              "b\r\nHello World\r\n0\r\n"
                                              "trailer1:t2\r\n"
                                              "\r\n",
                                              false));

  // Expect that upgrade headers are dropped and trailers are sent.
  response->waitForHeaders();
  EXPECT_EQ(nullptr, response->headers().Upgrade());
  EXPECT_EQ(nullptr, response->headers().Connection());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("Hello World", response->body());
  EXPECT_NE(response->trailers(), nullptr);
}

TEST_P(IntegrationTest, ConnectWithNoBody) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, false, false); });
  initialize();

  // Send the payload early so we can regression test that body data does not
  // get proxied until after the response headers are sent.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("CONNECT host.com:80 HTTP/1.1\r\n\r\npayload", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  EXPECT_TRUE(absl::StartsWith(data, "CONNECT host.com:80 HTTP/1.1"));
  // The payload should not be present as the response headers have not been sent.
  EXPECT_FALSE(absl::StrContains(data, "payload")) << data;
  // No transfer-encoding: chunked or connection: close
  EXPECT_FALSE(absl::StrContains(data, "hunked")) << data;
  EXPECT_FALSE(absl::StrContains(data, "onnection")) << data;

  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 200 OK\r\n")) << tcp_client->data();
  // Make sure the following payload is proxied without chunks or any other modifications.
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\npayload"), &data));

  ASSERT_TRUE(fake_upstream_connection->write("return-payload"));
  tcp_client->waitForData("\r\n\r\nreturn-payload", false);
  EXPECT_FALSE(absl::StrContains(tcp_client->data(), "hunked"));

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(IntegrationTest, ConnectWithChunkedBody) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, false, false); });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("CONNECT host.com:80 HTTP/1.1\r\n\r\npayload", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  // No transfer-encoding: chunked or connection: close
  EXPECT_FALSE(absl::StrContains(data, "hunked")) << data;
  EXPECT_FALSE(absl::StrContains(data, "onnection")) << data;
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 200 OK\r\n")) << tcp_client->data();
  // Make sure the following payload is proxied without chunks or any other modifications.
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\npayload"), &data));

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(IntegrationTest, ConnectWithTEChunked) {
#ifdef ENVOY_ENABLE_UHV
  config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                    "true");
#endif
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, false, false); });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write(
      "CONNECT host.com:80 HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\npayload", false));

  tcp_client->waitForData("\r\n\r\n", false);
#ifdef ENVOY_ENABLE_UHV
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 400 Bad Request\r\n"))
      << tcp_client->data();
#else
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 501 Not Implemented\r\n"))
      << tcp_client->data();
#endif
  tcp_client->close();
}

// Verifies that a 204 response returns without a body
TEST_P(IntegrationTest, Response204WithBody) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // Create a response with a body. This will cause an upstream messaging error but downstream
  // should still see a response.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "204"}}, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("204"));
  // The body should be removed
  EXPECT_EQ(0, response->body().size());
}

TEST_P(IntegrationTest, QuitQuitQuit) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  initialize();
  test_server_->useAdminInterfaceToQuit(true);
}

// override_stream_error_on_invalid_http_message=true and HCM
// stream_error_on_invalid_http_message=false: test that HTTP/1.1 connection is left open on invalid
// HTTP message (missing :host header)
TEST_P(IntegrationTest, ConnectionIsLeftOpenIfHCMStreamErrorIsFalseAndOverrideIsTrue) {
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(false);
        hcm.mutable_http_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/test/long/url"}, {"content-length", "0"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_FALSE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// override_stream_error_on_invalid_http_message is not set and HCM
// stream_error_on_invalid_http_message=true: test that HTTP/1.1 connection is left open on invalid
// HTTP message (missing :host header)
TEST_P(IntegrationTest, ConnectionIsLeftOpenIfHCMStreamErrorIsTrueAndOverrideNotSet) {
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_stream_error_on_invalid_http_message()->set_value(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/test/long/url"}, {"content-length", "0"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_FALSE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// override_stream_error_on_invalid_http_message is not set and HCM
// stream_error_on_invalid_http_message=false: test that HTTP/1.1 connection is terminated on
// invalid HTTP message (missing :host header)
TEST_P(IntegrationTest, ConnectionIsTerminatedIfHCMStreamErrorIsFalseAndOverrideNotSet) {
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(false);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/test/long/url"}, {"content-length", "0"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(IntegrationTest, Preconnect) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_preconnect_policy()
        ->mutable_predictive_preconnect_ratio()
        ->set_value(1.5);
  });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();
    for (int i = 0; i < 5; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });

  setUpstreamCount(5);
  initialize();

  std::list<IntegrationCodecClientPtr> clients;
  std::list<Http::RequestEncoder*> encoders;
  std::list<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeHttpConnectionPtr> fake_connections{15};

  int upstream_index = 0;
  for (uint32_t i = 0; i < 10; ++i) {
    // Start a new request.
    clients.push_back(makeHttpConnection(lookupPort("http")));
    auto encoder_decoder = clients.back()->startRequest(default_request_headers_);
    encoders.push_back(&encoder_decoder.first);
    responses.push_back(std::move(encoder_decoder.second));

    // For each HTTP request, a new connection will be established, as none of
    // the streams are closed so no connections can be reused.
    waitForNextUpstreamConnection(std::vector<uint64_t>({0, 1, 2, 3, 4}),
                                  TestUtility::DefaultTimeout, fake_connections[upstream_index]);
    ++upstream_index;

    // For every other connection, an extra connection should be preconnected.
    if (i % 2 == 0) {
      waitForNextUpstreamConnection(std::vector<uint64_t>({0, 1, 2, 3, 4}),
                                    TestUtility::DefaultTimeout, fake_connections[upstream_index]);
      ++upstream_index;
    }
  }

  // Clean up.
  while (!clients.empty()) {
    clients.front()->close();
    clients.pop_front();
  }

  for (auto& connection : fake_connections) {
    ASSERT_TRUE(connection->close());
    ASSERT_TRUE(connection->waitForDisconnect());
    connection.reset();
  }
}

TEST_P(IntegrationTest, RandomPreconnect) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_preconnect_policy()
        ->mutable_predictive_preconnect_ratio()
        ->set_value(1.5);
  });
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();
    for (int i = 0; i < 5; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });

  setUpstreamCount(5);
  TestRandomGenerator rand;
  autonomous_upstream_ = true;
  initialize();

  std::list<IntegrationCodecClientPtr> clients;
  std::list<Http::RequestEncoder*> encoders;
  std::list<IntegrationStreamDecoderPtr> responses;
  const uint32_t num_requests = 50;

  for (uint32_t i = 0; i < num_requests; ++i) {
    if (rand.random() % 5 <= 3) { // Bias slightly towards more connections
      // Start a new request.
      clients.push_back(makeHttpConnection(lookupPort("http")));
      auto encoder_decoder = clients.back()->startRequest(default_request_headers_);
      encoders.push_back(&encoder_decoder.first);
      responses.push_back(std::move(encoder_decoder.second));
    } else if (!clients.empty()) {
      // Finish up a request.
      clients.front()->sendData(*encoders.front(), 0, true);
      encoders.pop_front();
      ASSERT_TRUE(responses.front()->waitForEndStream());
      responses.pop_front();
      clients.front()->close();
      clients.pop_front();
    }
  }
  // Clean up.
  while (!clients.empty()) {
    clients.front()->sendData(*encoders.front(), 0, true);
    encoders.pop_front();
    ASSERT_TRUE(responses.front()->waitForEndStream());
    responses.pop_front();
    clients.front()->close();
    clients.pop_front();
  }
}

class TestRetryOptionsPredicateFactory : public Upstream::RetryOptionsPredicateFactory {
public:
  Upstream::RetryOptionsPredicateConstSharedPtr
  createOptionsPredicate(const Protobuf::Message&,
                         Upstream::RetryExtensionFactoryContext&) override {
    return std::make_shared<TestPredicate>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom empty config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "test_retry_options_predicate_factory"; }

private:
  struct TestPredicate : public Upstream::RetryOptionsPredicate {
    UpdateOptionsReturn updateOptions(const UpdateOptionsParameters&) const override {
      UpdateOptionsReturn ret;
      Network::TcpKeepaliveConfig tcp_keepalive_config;
      tcp_keepalive_config.keepalive_probes_ = 1;
      tcp_keepalive_config.keepalive_time_ = 1;
      tcp_keepalive_config.keepalive_interval_ = 1;
      ret.new_upstream_socket_options_ =
          Network::SocketOptionFactory::buildTcpKeepaliveOptions(tcp_keepalive_config);
      return ret;
    }
  };
};

// Verify that a test retry options predicate starts a new connection pool with a new connection.
TEST_P(IntegrationTest, RetryOptionsPredicate) {
  TestRetryOptionsPredicateFactory factory;
  Registry::InjectFactory<Upstream::RetryOptionsPredicateFactory> registered(factory);

  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0)->mutable_route();
        auto* retry_policy = route->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        auto* predicate = retry_policy->add_retry_options_predicates();
        predicate->set_name("test_retry_options_predicate_factory");
        predicate->mutable_typed_config()->set_type_url(
            "type.googleapis.com/google.protobuf.Struct");
      });

  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/some/path"},
      {":scheme", "http"},
      {":authority", "cluster_0"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  // Force a retry and run the predicate
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  // Using a different socket option will cause a new connection pool to be used and a new
  // connection.
  FakeHttpConnectionPtr new_upstream_connection;
  FakeStreamPtr new_upstream_request;
  result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, new_upstream_connection);
  RELEASE_ASSERT(result, result.message());
  result = new_upstream_connection->waitForNewStream(*dispatcher_, new_upstream_request);
  RELEASE_ASSERT(result, result.message());
  result = new_upstream_request->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  new_upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  result = response->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  result = new_upstream_connection->close();
  RELEASE_ASSERT(result, result.message());
  result = new_upstream_connection->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
}

// Tests that a filter (set-route-filter) using the setRoute callback and DelegatingRoute mechanism
// successfully overrides the cached route, and subsequently, the request's upstream cluster
// selection.
TEST_P(IntegrationTest, SetRouteToDelegatingRouteWithClusterOverride) {
  useAccessLog("%UPSTREAM_CLUSTER%");

  config_helper_.prependFilter(R"EOF(
    name: set-route-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SetRouteFilterConfig
      cluster_override: cluster_override
    )EOF");

  setUpstreamCount(2);

  // Tests with ORIGINAL_DST cluster because the first use case of the setRoute / DelegatingRoute
  // route mutability functionality will be for a filter that re-routes requests to an
  // ORIGINAL_DST cluster on a per-request basis.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    std::string cluster_yaml = R"EOF(
            name: cluster_override
            connect_timeout: 1.250s
            type: ORIGINAL_DST
            lb_policy: CLUSTER_PROVIDED
            original_dst_lb_config:
              use_http_header: true
          )EOF";
    envoy::config::cluster::v3::Cluster cluster_config;
    TestUtility::loadFromYaml(cluster_yaml, cluster_config);
    auto* orig_dst_cluster = bootstrap.mutable_static_resources()->add_clusters();
    orig_dst_cluster->MergeFrom(cluster_config);
  });

  auto co_vhost =
      config_helper_.createVirtualHost("cluster_override vhost", "/some/path", "cluster_override");
  config_helper_.addVirtualHost(co_vhost);

  initialize();

  const std::string ip_port_pair =
      absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":",
                   fake_upstreams_[1]->localAddress()->ip()->port());

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/some/path"},
      {":scheme", "http"},
      {":authority", "cluster_0"},
      {"x-envoy-original-dst-host", ip_port_pair},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Setting the upstream_index argument to 1 here tests that we get a request on
  // fake_upstreams_[1], which implies traffic is going to cluster_override. This is because
  // cluster_override, being an ORIGINAL DST cluster, will route the request to the IP/port
  // specified in the x-envoy-original-dst-host header (in this test case, port taken from
  // fake_upstreams_[1]).
  auto response =
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0, 1);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Even though headers specify cluster_0, set_route_filter modifies cached route cluster of
  // current request to cluster_override.
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_total")->value(), 0);

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_override.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_override.upstream_rq_200")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("cluster_override"));
}

ConfigHelper::HttpModifierFunction setAppendXForwardedPort(bool append_x_forwarded_port,
                                                           bool use_remote_address = false,
                                                           uint32_t xff_num_trusted_hops = 0) {
  return
      [append_x_forwarded_port, use_remote_address, xff_num_trusted_hops](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_use_remote_address()->set_value(use_remote_address);
        hcm.set_xff_num_trusted_hops(xff_num_trusted_hops);
        hcm.set_append_x_forwarded_port(append_x_forwarded_port);
      };
}

// Verify when append_x_forwarded_port is turned on, the x-forwarded-port header should be appended.
TEST_P(IntegrationTest, AppendXForwardedPort) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(true));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), Not(HeaderValueOf(Headers::get().ForwardedPort, "")));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

// Verify when append_x_forwarded_port is not turned on, the x-forwarded-port header should not be
// appended.
TEST_P(IntegrationTest, DoNotAppendXForwardedPort) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(false));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers().ForwardedPort(), nullptr);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

// Verify when the x-forwarded-port header has been set, the x-forwarded-port header should not be
// appended.
TEST_P(IntegrationTest, IgnoreAppendingXForwardedPortIfHasBeenSet) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(true));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"},
                                     {"x-forwarded-port", "8080"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().ForwardedPort, "8080"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

// Verify when append_x_forwarded_port is turned on, the x-forwarded-port header from trusted hop
// will be preserved.
TEST_P(IntegrationTest, PreserveXForwardedPortFromTrustedHop) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(true, true, 1));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"},
                                     {"x-forwarded-port", "80"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().ForwardedPort, "80"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

// Verify when append_x_forwarded_port is turned on, the x-forwarded-port header from untrusted hop
// will be overwritten.
TEST_P(IntegrationTest, OverwriteXForwardedPortFromUntrustedHop) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(true, true, 0));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"},
                                     {"x-forwarded-port", "80"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), Not(HeaderValueOf(Headers::get().ForwardedPort, "80")));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

// Verify when append_x_forwarded_port is not turned on, the x-forwarded-port header from untrusted
// hop will not be overwritten.
TEST_P(IntegrationTest, DoNotOverwriteXForwardedPortFromUntrustedHop) {
  config_helper_.addConfigModifier(setAppendXForwardedPort(false, true, 0));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"connection", "close"},
                                     {"x-forwarded-port", "80"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().ForwardedPort, "80"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

TEST_P(IntegrationTest, TestDuplicatedContentLengthSameValue) {
  config_helper_.disableDelayClose();
  initialize();

  std::string response;
  const std::string full_request = "POST / HTTP/1.1\r\n"
                                   "Host: host\r\n"
                                   "content-length: 20\r\n"
                                   "content-length: 20\r\n\r\n";
  sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, TestDuplicatedContentLengthDifferentValue) {
  config_helper_.disableDelayClose();
  initialize();

  std::string response;
  const std::string full_request = "POST / HTTP/1.1\r\n"
                                   "Host: host\r\n"
                                   "content-length: 20\r\n"
                                   "content-length: 53\r\n\r\n";
  sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
  EXPECT_THAT(response, StartsWith("HTTP/1.1 400 Bad Request\r\n"));
}

} // namespace Envoy

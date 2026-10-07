#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "contrib/istio/filters/network/hbone/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

using testing::Eq;
using testing::HasSubstr;

class HboneIntegrationTest : public testing::TestWithParam<bool>, public HttpIntegrationTest {
public:
  HboneIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initialize() override {
    config_helper_.addRuntimeOverride("envoy.restart_features.upstream_http_filters_with_tcp_proxy",
                                      GetParam() ? "true" : "false");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener internal_listener;
      auto* extension = bootstrap.add_bootstrap_extensions();
      extension->set_name("envoy.bootstrap.internal_listener");
      ASSERT_TRUE(extension->mutable_typed_config()->PackFrom(internal_listener));

      auto* resources = bootstrap.mutable_static_resources();
      auto* service = resources->add_clusters();
      service->set_name("service");
      service->mutable_connect_timeout()->set_seconds(5);
      service->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* transport = service->mutable_transport_socket();
      transport->set_name("envoy.transport_sockets.istio_hbone");
      ASSERT_TRUE(transport->mutable_typed_config()->PackFrom(SocketConfig{}));
      auto* assignment = service->mutable_load_assignment();
      assignment->set_cluster_name("service");
      auto* endpoints = assignment->add_endpoints();

      // Distinct application hosts share the same outer HTTP/2 connection.
      for (int i = 0; i < 2; ++i) {
        const auto name = absl::StrCat("originate_", i);
        auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
        endpoint->mutable_address()->mutable_envoy_internal_address()->set_server_listener_name(
            name);
        auto* listener = resources->add_listeners();
        listener->set_name(name);
        listener->mutable_internal_listener();
        auto* filter = listener->add_filter_chains()->add_filters();
        filter->set_name("envoy.filters.network.istio_hbone");
        FilterConfig config;
        auto* tcp = config.mutable_tcp_proxy();
        tcp->set_stat_prefix(name);
        tcp->set_cluster("cluster_0");
        tcp->mutable_tunneling_config()->set_hostname(absl::StrCat("app-", i, ":8080"));
        ASSERT_TRUE(filter->mutable_typed_config()->PackFrom(config));
      }
    });
    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()
                            ->mutable_virtual_hosts(0)
                            ->mutable_routes(0)
                            ->mutable_route();
          route->set_cluster("service");
          if (retry_) {
            auto* retry = route->mutable_retry_policy();
            retry->set_retry_on("connect-failure");
            retry->mutable_num_retries()->set_value(1);
            retry->set_host_selection_retry_max_attempts(3);
            auto* predicate = retry->add_retry_host_predicate();
            predicate->set_name("envoy.retry_host_predicates.previous_hosts");
            ASSERT_TRUE(predicate->mutable_typed_config()->PackFrom(
                envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate{}));
          }
        });
    HttpIntegrationTest::initialize();
  }

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void waitForConnect(FakeStreamPtr& stream) {
    if (!fake_upstream_connection_) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, stream));
    ASSERT_TRUE(stream->waitForHeadersComplete());
    EXPECT_EQ("CONNECT", stream->headers().getMethodValue());
    EXPECT_EQ(0, stream->bodyLength());
  }

  void acceptAndRespond(FakeStream& stream, IntegrationStreamDecoder& response) {
    stream.encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    ASSERT_TRUE(stream.waitForData(*dispatcher_, [](const std::string& data) {
      return absl::StrContains(data, "\r\n\r\npayload");
    })) << stream.body().toString();
    EXPECT_THAT(stream.body().toString(), HasSubstr("POST / HTTP/1.1\r\n"));
    stream.encodeData("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", false);
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_EQ("200", response.headers().getStatusValue());
    EXPECT_EQ("ok", response.body());
  }

  const Http::TestRequestHeaderMapImpl request_headers_{{":method", "POST"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "service"},
                                                        {"content-length", "7"}};
  bool retry_{false};
};

INSTANTIATE_TEST_SUITE_P(TcpProxyPoolImplementations, HboneIntegrationTest, testing::Bool());

TEST_P(HboneIntegrationTest, ConnectRejectionIsConnectionFailureWithoutPayload) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  test_server_->waitForCounter("cluster.service.upstream_cx_connect_fail", Eq(1));
}

TEST_P(HboneIntegrationTest, RetriesPostOnAnotherHostAfterConnectRejection) {
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr rejected;
  waitForConnect(rejected);
  const std::string rejected_host(rejected->headers().getHostValue());
  rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  waitForConnect(upstream_request_);
  EXPECT_NE(rejected_host, upstream_request_->headers().getHostValue());
  acceptAndRespond(*upstream_request_, *response);
  EXPECT_EQ(0, rejected->bodyLength());
  test_server_->waitForCounter("cluster.service.upstream_rq_retry", Eq(1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(1));
}

TEST_P(HboneIntegrationTest, Connect200WithEndStreamDoesNotReleasePayload) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  test_server_->waitForCounter("cluster.service.upstream_cx_connect_fail", Eq(1));
}

TEST_P(HboneIntegrationTest, ResetBeforeConnectAcceptanceDoesNotReleasePayload) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeResetStream();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  test_server_->waitForCounter("cluster.service.upstream_cx_connect_fail", Eq(1));
}

TEST_P(HboneIntegrationTest, ConcurrentConnectStreamsFailIndependently) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);

  auto second_client = makeHttpConnection(lookupPort("http"));
  auto second_response = second_client->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr rejected;
  waitForConnect(rejected);
  rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(second_response->waitForEndStream());
  EXPECT_EQ("503", second_response->headers().getStatusValue());
  acceptAndRespond(*upstream_request_, *response);
  EXPECT_EQ(0, rejected->bodyLength());
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(1));
  second_client->close();
}

TEST_P(HboneIntegrationTest, GracefulGoawayPreservesAcceptedStream) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  fake_upstream_connection_->encodeGoAway();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, [](const std::string& data) {
    return absl::StrContains(data, "\r\n\r\npayload");
  }));
  upstream_request_->encodeData("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", false);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("ok", response->body());
}

TEST_P(HboneIntegrationTest, NoConnectFailureRetryAfterApplicationBytesWereSent) {
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, [](const std::string& data) {
    return absl::StrContains(data, "\r\n\r\npayload");
  }));
  upstream_request_->encodeResetStream();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_cx_connect_fail")->value());
}

} // namespace
} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy

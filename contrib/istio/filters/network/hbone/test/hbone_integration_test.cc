#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "contrib/istio/filters/network/hbone/source/config.h"
#include "contrib/istio/filters/network/hbone/source/pool.h"
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
    // These tests exercise the data-stream exchange used by the double-HBONE listener.
    config_helper_.addRuntimeOverride("envoy.contrib.peer_metadata.disable_tls_registry", "true");
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::bootstrap::internal_listener::v3::InternalListener internal_listener;
      auto* extension = bootstrap.add_bootstrap_extensions();
      extension->set_name("envoy.bootstrap.internal_listener");
      ASSERT_TRUE(extension->mutable_typed_config()->PackFrom(internal_listener));

      auto* resources = bootstrap.mutable_static_resources();
      if (tcp_downstream_) {
        auto* listener = resources->add_listeners();
        listener->set_name("tcp");
        listener->mutable_address()->mutable_socket_address()->set_address("127.0.0.1");
        listener->mutable_address()->mutable_socket_address()->set_port_value(0);
        auto* filter = listener->add_filter_chains()->add_filters();
        filter->set_name("envoy.filters.network.tcp_proxy");
        envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp;
        tcp.set_stat_prefix("tcp");
        tcp.set_cluster("service");
        tcp.mutable_max_connect_attempts()->set_value(3);
        if (tcp_early_data_) {
          tcp.mutable_max_early_data_bytes()->set_value(1024);
        }
        ASSERT_TRUE(filter->mutable_typed_config()->PackFrom(tcp));
      }
      auto* service = resources->add_clusters();
      service->set_name("service");
      service->mutable_connect_timeout()->set_seconds(5);
      service->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      service->mutable_upstream_config()->set_name("envoy.upstreams.istio_hbone");
      ASSERT_TRUE(
          service->mutable_upstream_config()->mutable_typed_config()->PackFrom(PoolConfig{}));
      if (outlier_detection_) {
        for (auto& cluster : *resources->mutable_clusters()) {
          auto* outlier = cluster.mutable_outlier_detection();
          outlier->set_split_external_local_origin_errors(true);
          outlier->mutable_consecutive_local_origin_failure()->set_value(1);
          outlier->mutable_enforcing_consecutive_local_origin_failure()->set_value(100);
          outlier->mutable_consecutive_5xx()->set_value(1);
          outlier->mutable_max_ejection_percent()->set_value(100);
        }
      }
      auto* transport = service->mutable_transport_socket();
      transport->set_name("envoy.transport_sockets.istio_hbone");
      ASSERT_TRUE(transport->mutable_typed_config()->PackFrom(SocketConfig{}));
      if (!peer_metadata_listener_.empty()) {
        auto* filter = service->add_filters();
        filter->set_name("upstream_hbone_peer_metadata");
        TestUtility::loadFromYaml(R"EOF(
"@type": type.googleapis.com/udpa.type.v1.TypedStruct
type_url: type.googleapis.com/envoy.extensions.network_filters.peer_metadata.UpstreamConfig
value: {}
)EOF",
                                  *filter->mutable_typed_config());
      }
      auto* assignment = service->mutable_load_assignment();
      assignment->set_cluster_name("service");
      auto* endpoints = assignment->add_endpoints();

      // Distinct application hosts share the same outer HTTP/2 connection.
      for (int i = 0; i < 2; ++i) {
        const auto name = peer_metadata_listener_.empty() ? absl::StrCat("originate_", i)
                                                          : peer_metadata_listener_;
        auto* endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
        endpoint->mutable_address()->mutable_envoy_internal_address()->set_server_listener_name(
            name);
        if (!peer_metadata_listener_.empty()) {
          endpoint->mutable_address()->mutable_envoy_internal_address()->set_endpoint_id(
              absl::StrCat("app-", i));
          if (i != 0) {
            continue;
          }
        }
        auto* listener = resources->add_listeners();
        listener->set_name(name);
        listener->mutable_internal_listener();
        auto* chain = listener->add_filter_chains();
        if (!peer_metadata_listener_.empty()) {
          auto* metadata = chain->add_filters();
          metadata->set_name("upstream_hbone_peer_metadata");
          TestUtility::loadFromYaml(R"EOF(
"@type": type.googleapis.com/udpa.type.v1.TypedStruct
type_url: type.googleapis.com/envoy.extensions.network_filters.peer_metadata.Config
value:
  baggage_key: io.istio.baggage
)EOF",
                                    *metadata->mutable_typed_config());
        }
        auto* filter = chain->add_filters();
        filter->set_name("envoy.filters.network.istio_hbone");
        FilterConfig config;
        auto* tcp = config.mutable_tcp_proxy();
        tcp->set_stat_prefix(name);
        tcp->mutable_max_connect_attempts()->set_value(tunnel_attempts_);
        tcp->set_cluster("cluster_0");
        tcp->mutable_tunneling_config()->set_hostname(absl::StrCat("app-", i, ":8080"));
        tcp->mutable_tunneling_config()->set_propagate_response_headers(
            !peer_metadata_listener_.empty());
        if (nested_) {
          const auto outer_name = absl::StrCat("outer_", i);
          const auto inner_name = absl::StrCat("inner_", i);
          auto* outer_listener = resources->add_listeners();
          outer_listener->set_name(outer_name);
          outer_listener->mutable_internal_listener();
          auto* outer_filter = outer_listener->add_filter_chains()->add_filters();
          outer_filter->set_name("envoy.filters.network.istio_hbone");
          ASSERT_TRUE(outer_filter->mutable_typed_config()->PackFrom(config));
          tcp->set_cluster(inner_name);

          auto inner = resources->clusters(0);
          inner.set_name(inner_name);
          inner.mutable_load_assignment()->Clear();
          inner.mutable_load_assignment()->set_cluster_name(inner_name);
          inner.mutable_load_assignment()
              ->add_endpoints()
              ->add_lb_endpoints()
              ->mutable_endpoint()
              ->mutable_address()
              ->mutable_envoy_internal_address()
              ->set_server_listener_name(outer_name);
          SocketConfig socket;
          envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls;
          tls.mutable_common_tls_context()->add_alpn_protocols("h2");
          socket.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
          ASSERT_TRUE(socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls));
          inner.mutable_transport_socket()->set_name("envoy.transport_sockets.istio_hbone");
          ASSERT_TRUE(inner.mutable_transport_socket()->mutable_typed_config()->PackFrom(socket));
          *resources->add_clusters() = inner;
        }
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
            retry->set_retry_on(retry_on_);
            if (retry_on_ != "connect-failure") {
              retry->add_retriable_status_codes(403);
              retry->add_retriable_status_codes(429);
            }
            retry->mutable_num_retries()->set_value(1);
            retry->set_host_selection_retry_max_attempts(3);
            auto* predicate = retry->add_retry_host_predicate();
            predicate->set_name("envoy.retry_host_predicates.previous_hosts");
            ASSERT_TRUE(predicate->mutable_typed_config()->PackFrom(
                envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate{}));
          }
        });
    HttpIntegrationTest::initialize();
    if (tcp_downstream_) {
      registerTestServerPorts({"http", "tcp"});
    }
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

  void acceptAndRespond(FakeStream& stream, IntegrationStreamDecoder& response,
                        bool with_baggage = false) {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
    if (with_baggage) {
      headers.addCopy("baggage", "k8s.namespace.name=default,k8s.cluster.name=remote");
    }
    stream.encodeHeaders(headers, false);
    ASSERT_TRUE(stream.waitForData(*dispatcher_, [](const std::string& data) {
      return absl::StrContains(data, "\r\n\r\npayload");
    })) << stream.body().toString();
    EXPECT_THAT(stream.body().toString(), HasSubstr("POST / HTTP/1.1\r\n"));
    stream.encodeData("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", false);
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_EQ("200", response.headers().getStatusValue());
    EXPECT_EQ("ok", response.body());
  }

  void testPeerMetadataResponse(absl::string_view listener_name, bool with_baggage) {
    peer_metadata_listener_ = listener_name;
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
    waitForConnect(upstream_request_);
    acceptAndRespond(*upstream_request_, *response, with_baggage);
    EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_cx_protocol_error")->value());
    EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
  }

  const Http::TestRequestHeaderMapImpl request_headers_{{":method", "POST"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "service"},
                                                        {"content-length", "7"}};
  bool retry_{false};
  bool outlier_detection_{false};
  bool tcp_downstream_{false};
  bool tcp_early_data_{false};
  bool nested_{false};
  uint32_t tunnel_attempts_{1};
  std::string retry_on_{"connect-failure"};
  std::string peer_metadata_listener_;
  std::vector<FakeStreamPtr> completed_streams_;
};

INSTANTIATE_TEST_SUITE_P(TcpProxyPoolImplementations, HboneIntegrationTest, testing::Bool());

TEST_P(HboneIntegrationTest, SingleHbonePeerMetadataIsRemovedBeforeHttpResponse) {
  testPeerMetadataResponse("connect_originate_hbone_shim", true);
}

TEST_P(HboneIntegrationTest, InnerHbonePeerMetadataIsRemovedBeforeHttpResponse) {
  testPeerMetadataResponse("inner_connect_originate_hbone_shim", true);
}

TEST_P(HboneIntegrationTest, SingleHboneEmptyPeerMetadataIsRemovedBeforeHttpResponse) {
  testPeerMetadataResponse("connect_originate_hbone_shim", false);
}

TEST_P(HboneIntegrationTest, InnerHboneEmptyPeerMetadataIsRemovedBeforeHttpResponse) {
  testPeerMetadataResponse("inner_connect_originate_hbone_shim", false);
}

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

TEST_P(HboneIntegrationTest, PermanentConnectRejectionsDoNotRetryOrEject) {
  retry_ = true;
  retry_on_ = "connect-failure,reset,reset-before-request,5xx,retriable-status-codes";
  tunnel_attempts_ = 3;
  outlier_detection_ = true;
  initialize();
  for (const auto status : {"400", "401", "403", "404", "429", "302", "204", "600", "999"}) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
    FakeStreamPtr rejected;
    waitForConnect(rejected);
    rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", status}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(status[0] == '4' ? status : "502", response->headers().getStatusValue());
    EXPECT_EQ(0, rejected->bodyLength());
    codec_client_->close();
    completed_streams_.push_back(std::move(rejected));
  }
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
  EXPECT_EQ(9, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  for (const auto cluster : {"service", "cluster_0"}) {
    EXPECT_EQ(0, test_server_
                     ->counter(absl::StrCat(
                         "cluster.", cluster,
                         ".outlier_detection.ejections_detected_consecutive_local_origin_failure"))
                     ->value());
    EXPECT_EQ(0,
              test_server_
                  ->counter(absl::StrCat("cluster.", cluster,
                                         ".outlier_detection.ejections_detected_consecutive_5xx"))
                  ->value());
  }
}

TEST_P(HboneIntegrationTest, AllConnectServerErrorsRemainRetryable) {
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (const auto status : {"500", "502", "503", "504", "599"}) {
    auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
    FakeStreamPtr rejected;
    waitForConnect(rejected);
    rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", status}}, true);
    FakeStreamPtr accepted;
    waitForConnect(accepted);
    accepted->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    ASSERT_TRUE(accepted->waitForData(*dispatcher_, [](const std::string& data) {
      return absl::StrContains(data, "\r\n\r\npayload");
    }));
    // Close the application connection so the next request needs a new CONNECT.
    accepted->encodeData("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
                         true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(0, rejected->bodyLength());
    completed_streams_.push_back(std::move(rejected));
    completed_streams_.push_back(std::move(accepted));
  }
  EXPECT_EQ(5, test_server_->counter("cluster.service.upstream_rq_retry")->value());
}

TEST_P(HboneIntegrationTest, ApplicationForbiddenRetainsApplicationRetryPolicy) {
  retry_ = true;
  retry_on_ = "retriable-status-codes";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr first;
  waitForConnect(first);
  first->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ASSERT_TRUE(first->waitForData(*dispatcher_, [](const std::string& data) {
    return absl::StrContains(data, "\r\n\r\npayload");
  }));
  first->encodeData("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                    true);
  waitForConnect(upstream_request_);
  acceptAndRespond(*upstream_request_, *response);
  EXPECT_EQ(1, test_server_->counter("cluster.service.upstream_rq_retry")->value());
}

TEST_P(HboneIntegrationTest, PermanentRejectionDoesNotAffectConcurrentAcceptedStream) {
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  auto other_client = makeHttpConnection(lookupPort("http"));
  auto other_response = other_client->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr rejected;
  waitForConnect(rejected);
  rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  ASSERT_TRUE(other_response->waitForEndStream());
  EXPECT_EQ("403", other_response->headers().getStatusValue());
  acceptAndRespond(*upstream_request_, *response);
  EXPECT_EQ(0, rejected->bodyLength());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
  other_client->close();
}

TEST_P(HboneIntegrationTest, RawTcpRejectionClosesWithoutRetryOrPayload) {
  tcp_downstream_ = true;
  outlier_detection_ = true;
  tunnel_attempts_ = 3;
  initialize();
  auto client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(client->write("payload"));
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  client->waitForDisconnect();
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_EQ(1, test_server_->counter("cluster.service.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_
                   ->counter("cluster.service.outlier_detection.ejections_detected_consecutive_"
                             "local_origin_failure")
                   ->value());
}

TEST_P(HboneIntegrationTest, RawTcpTransientFailureRetriesAndForwards) {
  tcp_downstream_ = true;
  initialize();
  auto client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(client->write("payload"));
  FakeStreamPtr rejected;
  waitForConnect(rejected);
  rejected->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "payload"));
  upstream_request_->encodeData("reply", false);
  client->waitForData("reply");
  EXPECT_EQ(0, rejected->bodyLength());
  client->close();
}

TEST_P(HboneIntegrationTest, RawTcpCancelPendingConnectThenRejectAnotherConnection) {
  tcp_downstream_ = true;
  // Keep downstream reads enabled so TCP proxy observes the reset while CONNECT
  // is pending, instead of waiting until upstream establishment finishes.
  tcp_early_data_ = true;
  initialize();
  auto cancelled = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(cancelled->write("cancelled"));
  waitForConnect(upstream_request_);
  cancelled->close(Network::ConnectionCloseType::AbortReset);
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_cx_destroy", Eq(1));
  // A late CONNECT rejection must not call the destroyed downstream connection.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  ASSERT_TRUE(upstream_request_->waitForReset());
  EXPECT_EQ(0, upstream_request_->bodyLength());

  auto client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(client->write("payload"));
  FakeStreamPtr denied;
  waitForConnect(denied);
  denied->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  client->waitForDisconnect();
  EXPECT_EQ(0, denied->bodyLength());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

TEST_P(HboneIntegrationTest, Http2PermanentRejectionDoesNotCancelOtherRequest) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  auto denied_response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr denied;
  waitForConnect(denied);
  denied->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, false);
  ASSERT_TRUE(denied_response->waitForEndStream());
  EXPECT_EQ("403", denied_response->headers().getStatusValue());
  ASSERT_TRUE(denied->waitForReset());
  acceptAndRespond(*upstream_request_, *response);
  EXPECT_EQ(0, denied->bodyLength());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
}

TEST_P(HboneIntegrationTest, CancelPendingConnectThenRejectAnotherRequest) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request = codec_client_->startRequest(request_headers_);
  waitForConnect(upstream_request_);
  codec_client_->sendReset(request.first);
  // The stock HTTP pool can retain a connecting socket after its last waiter
  // cancels. A subsequent rejection must not call the destroyed router request.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  ASSERT_TRUE(upstream_request_->waitForReset());
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  FakeStreamPtr denied;
  waitForConnect(denied);
  denied->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ(0, denied->bodyLength());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
}

TEST_P(HboneIntegrationTest, OuterConnectRejectionPreservesStatusThroughInnerTlsPool) {
  nested_ = true;
  outlier_detection_ = true;
  tunnel_attempts_ = 3;
  retry_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, "payload");
  waitForConnect(upstream_request_);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "403"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());
  // Even inner TLS bytes are held until the outer CONNECT succeeds.
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.service.upstream_rq_retry")->value());
  for (const auto cluster : {"service", "cluster_0", "inner_0", "inner_1"}) {
    EXPECT_EQ(0, test_server_
                     ->counter(absl::StrCat(
                         "cluster.", cluster,
                         ".outlier_detection.ejections_detected_consecutive_local_origin_failure"))
                     ->value());
  }
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

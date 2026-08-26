#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {
namespace {

class ExtAuthzUdpIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                   public BaseIntegrationTest {
public:
  ExtAuthzUdpIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseUdpListenerConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void createUpstreams() override {
    // fake_upstreams_[0] is the UDP proxy target; fake_upstreams_[1] is the gRPC authz server.
    BaseIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeFilter(bool failure_mode_allow) {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());

    config_helper_.addConfigModifier(
        [this, failure_mode_allow](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ext_authz_cluster->set_name("ext_authz");
          ConfigHelper::setHttp2(*ext_authz_cluster);

          envoy::extensions::filters::udp::udp_proxy::session::ext_authz::v3::FilterConfig
              ext_authz_config;
          ext_authz_config.set_stat_prefix("foo");
          ext_authz_config.set_failure_mode_allow(failure_mode_allow);
          // Enable buffering so the datagram that triggered the session is forwarded on allow.
          ext_authz_config.mutable_buffer_options();
          setGrpcService(*ext_authz_config.mutable_grpc_service(), "ext_authz",
                         fake_upstreams_.back()->localAddress());

          const std::string udp_proxy_yaml = R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
)EOF";
          envoy::config::listener::v3::ListenerFilter listener_filter;
          TestUtility::loadFromYaml(udp_proxy_yaml, listener_filter);

          envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig udp_proxy;
          std::ignore = listener_filter.typed_config().UnpackTo(&udp_proxy);
          auto* session_filter = udp_proxy.add_session_filters();
          session_filter->set_name("envoy.filters.udp.session.ext_authz");
          std::ignore = session_filter->mutable_typed_config()->PackFrom(ext_authz_config);
          std::ignore = listener_filter.mutable_typed_config()->PackFrom(udp_proxy);

          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->add_listener_filters()->CopyFrom(listener_filter);
        });

    BaseIntegrationTest::initialize();
  }

  void TearDown() override {
    if (fake_authz_connection_ != nullptr) {
      AssertionResult result = fake_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_authz_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_authz_connection_ = nullptr;
    }
  }

  void waitForAuthzRequest() {
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_authz_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_authz_connection_->waitForNewStream(*dispatcher_, authz_request_);
    RELEASE_ASSERT(result, result.message());
    result = authz_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  void sendAuthzResponse(Grpc::Status::GrpcStatus check_status) {
    authz_request_->startGrpcStream();
    envoy::service::auth::v3::CheckResponse response;
    response.mutable_status()->set_code(check_status);
    authz_request_->sendGrpcMessage(response);
    authz_request_->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  Network::Address::InstanceConstSharedPtr listenerAddress() {
    return *Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                    lookupPort("listener_0")));
  }

  FakeHttpConnectionPtr fake_authz_connection_;
  FakeStreamPtr authz_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtAuthzUdpIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// An OK authorization response admits the session and forwards the datagram upstream.
TEST_P(ExtAuthzUdpIntegrationTest, AllowedForwardsDatagram) {
  initializeFilter(/*failure_mode_allow=*/false);
  Network::Test::UdpSyncPeer client(version_);
  client.write("hello", *listenerAddress());

  waitForAuthzRequest();
  sendAuthzResponse(Grpc::Status::WellKnownGrpcStatus::Ok);

  Network::UdpRecvData request_datagram;
  ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
  EXPECT_EQ("hello", request_datagram.buffer_->toString());
  test_server_->waitForCounter("udp.session.ext_authz.foo.ok", 1);
}

// A denied authorization response drops the session; no datagram reaches the upstream.
TEST_P(ExtAuthzUdpIntegrationTest, DeniedDropsDatagram) {
  initializeFilter(/*failure_mode_allow=*/false);
  Network::Test::UdpSyncPeer client(version_);
  client.write("hello", *listenerAddress());

  waitForAuthzRequest();
  sendAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  test_server_->waitForCounter("udp.session.ext_authz.foo.denied", 1);
  Network::UdpRecvData request_datagram;
  EXPECT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::milliseconds(500)));
}

} // namespace
} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

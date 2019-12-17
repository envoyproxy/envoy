#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/core/protocol.pb.h"
#include "envoy/upstream/upstream.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {
class TLSIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public Event::TestUsingSimulatedTime,
                           public HttpIntegrationTest {
public:
  TLSIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void setup() {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto& cluster_config = bootstrap.mutable_static_resources()->mutable_clusters()->at(0);
      cluster_config.mutable_common_http_protocol_options()->set_auto_sni(true);
      cluster_config.mutable_connect_timeout()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));

      envoy::api::v2::auth::UpstreamTlsContext tls_context;
      auto* validation_context =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      cluster_config.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      cluster_config.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    });

    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        createUpstreamSslContext(), 0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::api::v2::auth::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
        fmt::format("test/config/integration/certs/{}cert.pem", upstream_cert_name_)));
    tls_cert->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
        fmt::format("test/config/integration/certs/{}key.pem", upstream_cert_name_)));

    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);

    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  std::string upstream_cert_name_{"upstreamlocalhost"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TLSIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TLSIntegrationTest, Test) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}

} // namespace
} // namespace Envoy

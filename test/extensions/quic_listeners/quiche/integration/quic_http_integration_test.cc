#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class QuicHttpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  QuicHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP3, GetParam(),
                            ConfigHelper::QUIC_HTTP_PROXY_CONFIG),
        supported_versions_(quic::CurrentSupportedVersions()),
        crypto_config_(std::make_unique<EnvoyQuicFakeProofVerifier>()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()) {}

  Network::ClientConnectionPtr makeClientConnection(uint32_t port) override {
    Network::Address::InstanceConstSharedPtr server_addr = Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Address::InstanceConstSharedPtr local_addr =
        Network::Test::getCanonicalLoopbackAddress(version_);
    auto connection = std::make_unique<EnvoyQuicClientConnection>(
        getNextServerDesignatedConnectionId(), server_addr, conn_helper_, alarm_factory_,
        supported_versions_, local_addr, *dispatcher_);
    auto session = std::make_unique<EnvoyQuicClientSession>(
        quic_config_, supported_versions_, std::move(connection), server_id_, &crypto_config_,
        &push_promise_index_, *dispatcher_, 0);
    return session;
  }

  quic::QuicConnectionId getNextServerDesignatedConnectionId() {
    quic::QuicCryptoClientConfig::CachedState* cached = crypto_config_.LookupOrCreate(server_id_);
    // If the cached state indicates that we should use a server-designated
    // connection ID, then return that connection ID.
    quic::QuicConnectionId conn_id = cached->has_server_designated_connection_id()
                                         ? cached->GetNextServerDesignatedConnectionId()
                                         : quic::EmptyQuicConnectionId();
    return conn_id.IsEmpty() ? quic::QuicUtils::CreateRandomConnectionId() : conn_id;
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      envoy::api::v2::auth::DownstreamTlsContext tls_context;
      ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context());
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* transport_socket = filter_chain->mutable_transport_socket();
      TestUtility::jsonConvert(tls_context, *transport_socket->mutable_config());
    });
    config_helper_.addConfigModifier(
        [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
               hcm) { hcm.mutable_delayed_close_timeout()->set_nanos(0); });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

protected:
  quic::QuicConfig quic_config_;
  quic::QuicServerId server_id_;
  quic::QuicClientPushPromiseIndex push_promise_index_;
  quic::ParsedQuicVersionVector supported_versions_;
  quic::QuicCryptoClientConfig crypto_config_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicHttpIntegrationTest, SimpleGetRequestResponse) {
  testRouterHeaderOnlyRequestAndResponse();
}

} // namespace Quic
} // namespace Envoy

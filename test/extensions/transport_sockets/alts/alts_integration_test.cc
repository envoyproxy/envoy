#include "common/common/thread.h"

#include "extensions/transport_sockets/alts/config.h"

#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/impl/codegen/service_type.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

class AltsIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AltsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initialize() override;
  void SetUp() override;
  void TearDown() override;

  Network::ClientConnectionPtr makeAltsConnection();
  std::string fakeHandshakerServerAddress();

  Thread::ThreadPtr fake_handshaker_server_thread_;
  std::unique_ptr<grpc::Server> fake_handshaker_server_;
  ConditionalInitializer fake_handshaker_server_ci_;
  int fake_handshaker_server_port_{};
  Network::TransportSocketFactoryPtr client_alts_;
};

std::string AltsIntegrationTest::fakeHandshakerServerAddress() {
  return absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":",
                      std::to_string(fake_handshaker_server_port_));
}

void AltsIntegrationTest::initialize() {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto transport_socket = bootstrap.mutable_static_resources()
                                ->mutable_listeners(0)
                                ->mutable_filter_chains(0)
                                ->mutable_transport_socket();
    const std::string yaml = R"EOF(
  name: envoy.transport_sockets.alts
  config:
    peer_service_accounts: [peer_identity]
    handshaker_service: ")EOF" +
                             fakeHandshakerServerAddress() + "\"";

    MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *transport_socket);
  });
  HttpIntegrationTest::initialize();
  registerTestServerPorts({"http"});
}

void AltsIntegrationTest::SetUp() {
  fake_handshaker_server_thread_ = std::make_unique<Thread::Thread>([this]() {
    std::unique_ptr<grpc::Service> service = grpc::gcp::CreateFakeHandshakerService();

    std::string server_address = Network::Test::getLoopbackAddressUrlString(version_) + ":0";
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                             &fake_handshaker_server_port_);
    builder.RegisterService(service.get());

    fake_handshaker_server_ = builder.BuildAndStart();
    fake_handshaker_server_ci_.setReady();
    fake_handshaker_server_->Wait();
  });

  fake_handshaker_server_ci_.waitReady();

  const std::string yaml =
      absl::StrCat("handshaker_service: \"", fakeHandshakerServerAddress(), "\"");

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  UpstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *config);

  ENVOY_LOG_MISC(info, "{}", config->DebugString());

  client_alts_ = factory.createTransportSocketFactory(*config, mock_factory_ctx);
}
void AltsIntegrationTest::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  dispatcher_->clearDeferredDeleteList();
  if (fake_handshaker_server_ != nullptr) {
    fake_handshaker_server_->Shutdown();
  }
  fake_handshaker_server_thread_->join();
}

Network::Address::InstanceConstSharedPtr getAddress(const Network::Address::IpVersion& version,
                                                    int port) {
  std::string url =
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version) + ":" + std::to_string(port);
  return Network::Utility::resolveUrl(url);
}

Network::ClientConnectionPtr AltsIntegrationTest::makeAltsConnection() {
  Network::Address::InstanceConstSharedPtr address = getAddress(version_, lookupPort("http"));
  return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                             client_alts_->createTransportSocket(), nullptr);
}

INSTANTIATE_TEST_CASE_P(IpVersions, AltsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(AltsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [this]() -> Network::ClientConnectionPtr {
    return makeAltsConnection();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

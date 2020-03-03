#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"

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

using ::testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

class AltsIntegrationTestBase : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  AltsIntegrationTestBase(const std::string& server_peer_identity,
                          const std::string& client_peer_identity, bool server_connect_handshaker,
                          bool client_connect_handshaker)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()),
        server_peer_identity_(server_peer_identity), client_peer_identity_(client_peer_identity),
        server_connect_handshaker_(server_connect_handshaker),
        client_connect_handshaker_(client_connect_handshaker) {}

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.alts");
      envoy::extensions::transport_sockets::alts::v3::Alts alts_config;
      if (!server_peer_identity_.empty()) {
        alts_config.add_peer_service_accounts(server_peer_identity_);
      }
      alts_config.set_handshaker_service(fakeHandshakerServerAddress(server_connect_handshaker_));
      transport_socket->mutable_typed_config()->PackFrom(alts_config);
    });
    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

  void SetUp() override {
    fake_handshaker_server_thread_ = api_->threadFactory().createThread([this]() {
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

    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
    // We fake the singleton manager for the client, since it doesn't need to manage ALTS global
    // state, this is done by the test server instead.
    // TODO(htuch): Make this a proper mock.
    class FakeSingletonManager : public Singleton::Manager {
    public:
      Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb) override {
        return nullptr;
      }
    };
    FakeSingletonManager fsm;
    ON_CALL(mock_factory_ctx, singletonManager()).WillByDefault(ReturnRef(fsm));
    UpstreamAltsTransportSocketConfigFactory factory;

    envoy::extensions::transport_sockets::alts::v3::Alts alts_config;
    alts_config.set_handshaker_service(fakeHandshakerServerAddress(client_connect_handshaker_));
    if (!client_peer_identity_.empty()) {
      alts_config.add_peer_service_accounts(client_peer_identity_);
    }
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();
    TestUtility::jsonConvert(alts_config, *config);
    ENVOY_LOG_MISC(info, "{}", config->DebugString());

    client_alts_ = factory.createTransportSocketFactory(*config, mock_factory_ctx);
  }

  void TearDown() override {
    HttpIntegrationTest::cleanupUpstreamAndDownstream();
    dispatcher_->clearDeferredDeleteList();
    if (fake_handshaker_server_ != nullptr) {
      fake_handshaker_server_->Shutdown();
    }
    fake_handshaker_server_thread_->join();
  }

  Network::ClientConnectionPtr makeAltsConnection() {
    Network::Address::InstanceConstSharedPtr address = getAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               client_alts_->createTransportSocket(nullptr),
                                               nullptr);
  }

  std::string fakeHandshakerServerAddress(bool connect_to_handshaker) {
    if (connect_to_handshaker) {
      return absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":",
                          std::to_string(fake_handshaker_server_port_));
    }
    return wrongHandshakerServerAddress();
  }

  std::string wrongHandshakerServerAddress() { return " "; }

  Network::Address::InstanceConstSharedPtr getAddress(const Network::Address::IpVersion& version,
                                                      int port) {
    std::string url =
        "tcp://" + Network::Test::getLoopbackAddressUrlString(version) + ":" + std::to_string(port);
    return Network::Utility::resolveUrl(url);
  }

  const std::string server_peer_identity_;
  const std::string client_peer_identity_;
  bool server_connect_handshaker_;
  bool client_connect_handshaker_;
  Thread::ThreadPtr fake_handshaker_server_thread_;
  std::unique_ptr<grpc::Server> fake_handshaker_server_;
  ConditionalInitializer fake_handshaker_server_ci_;
  int fake_handshaker_server_port_{};
  Network::TransportSocketFactoryPtr client_alts_;
};

class AltsIntegrationTestValidPeer : public AltsIntegrationTestBase {
public:
  // FakeHandshake server sends "peer_identity" as peer service account. Set this
  // information into config to pass validation.
  AltsIntegrationTestValidPeer()
      : AltsIntegrationTestBase("peer_identity", "",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ true) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestValidPeer,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that when received peer service account passes validation, the alts
// handshake succeeds.
TEST_P(AltsIntegrationTestValidPeer, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [this]() -> Network::ClientConnectionPtr {
    return makeAltsConnection();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

class AltsIntegrationTestEmptyPeer : public AltsIntegrationTestBase {
public:
  AltsIntegrationTestEmptyPeer()
      : AltsIntegrationTestBase("", "",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ true) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestEmptyPeer,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that when peer service account is not set into config, the alts
// handshake succeeds.
TEST_P(AltsIntegrationTestEmptyPeer, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [this]() -> Network::ClientConnectionPtr {
    return makeAltsConnection();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

class AltsIntegrationTestClientInvalidPeer : public AltsIntegrationTestBase {
public:
  AltsIntegrationTestClientInvalidPeer()
      : AltsIntegrationTestBase("", "invalid_client_identity",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ true) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestClientInvalidPeer,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that when client receives peer service account which does not match
// any account in config, the handshake will fail and client closes connection.
TEST_P(AltsIntegrationTestClientInvalidPeer, ClientValidationFail) {
  initialize();
  codec_client_ = makeRawHttpConnection(makeAltsConnection());
  EXPECT_FALSE(codec_client_->connected());
}

class AltsIntegrationTestServerInvalidPeer : public AltsIntegrationTestBase {
public:
  AltsIntegrationTestServerInvalidPeer()
      : AltsIntegrationTestBase("invalid_server_identity", "",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ true) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestServerInvalidPeer,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that when Envoy receives peer service account which does not match
// any account in config, the handshake will fail and Envoy closes connection.
TEST_P(AltsIntegrationTestServerInvalidPeer, ServerValidationFail) {
  initialize();

  testing::NiceMock<Network::MockConnectionCallbacks> client_callbacks;
  Network::ClientConnectionPtr client_conn = makeAltsConnection();
  client_conn->addConnectionCallbacks(client_callbacks);
  EXPECT_CALL(client_callbacks, onEvent(Network::ConnectionEvent::Connected));
  client_conn->connect();

  EXPECT_CALL(client_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

class AltsIntegrationTestClientWrongHandshaker : public AltsIntegrationTestBase {
public:
  AltsIntegrationTestClientWrongHandshaker()
      : AltsIntegrationTestBase("", "",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ false) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestClientWrongHandshaker,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that when client connects to the wrong handshaker server, handshake fails
// and connection closes.
TEST_P(AltsIntegrationTestClientWrongHandshaker, ConnectToWrongHandshakerAddress) {
  initialize();
  codec_client_ = makeRawHttpConnection(makeAltsConnection());
  EXPECT_FALSE(codec_client_->connected());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

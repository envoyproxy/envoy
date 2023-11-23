#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"

#include "source/common/common/thread.h"
#include "source/extensions/transport_sockets/alts/config.h"
#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "src/proto/grpc/gcp/handshaker.grpc.pb.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

#ifdef major
#undef major
#endif
#ifdef minor
#undef minor
#endif

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/server/transport_socket_factory_context.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/impl/codegen/service_type.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/sync_stream.h"
#include "gtest/gtest.h"

using ::grpc::Service;
using ::grpc::gcp::HandshakerService;
using ::testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

// Fake handshake messages.
constexpr char kClientInitFrame[] = "ClientInit";
constexpr char kServerFrame[] = "ServerInitAndFinished";
constexpr char kClientFinishFrame[] = "ClientFinished";
// Error messages.
constexpr char kInvalidFrameError[] = "Invalid input frame.";
constexpr char kWrongStateError[] = "Wrong handshake state.";

// Hollowed out implementation of HandshakerService that is dysfunctional, but
// responds correctly to the first client request, capturing client and server
// ALTS versions in the process.
class CapturingHandshakerService : public grpc::gcp::HandshakerService::Service {
public:
  CapturingHandshakerService() = default;

  grpc::Status
  DoHandshake(grpc::ServerContext*,
              grpc::ServerReaderWriter<grpc::gcp::HandshakerResp, grpc::gcp::HandshakerReq>* stream)
      override {
    grpc::gcp::HandshakerReq request;
    grpc::gcp::HandshakerResp response;
    while (stream->Read(&request)) {
      if (request.has_client_start()) {
        client_versions = request.client_start().rpc_versions();
        client_max_frame_size = request.client_start().max_frame_size();
        // Sets response to make first request successful.
        response.set_out_frames(kClientInitFrame);
        response.set_bytes_consumed(0);
        response.mutable_status()->set_code(grpc::StatusCode::OK);
      } else if (request.has_server_start()) {
        server_versions = request.server_start().rpc_versions();
        server_max_frame_size = request.server_start().max_frame_size();
        response.mutable_status()->set_code(grpc::StatusCode::CANCELLED);
      }
      stream->Write(response);
      request.Clear();
      if (response.has_status()) {
        return grpc::Status::OK;
      }
    }
    return grpc::Status::OK;
  }

  // Storing client and server RPC versions for later verification.
  grpc::gcp::RpcProtocolVersions client_versions;
  grpc::gcp::RpcProtocolVersions server_versions;

  size_t client_max_frame_size{0};
  size_t server_max_frame_size{0};
};

// FakeHandshakeService implements a fake handshaker service using a fake key
// exchange protocol. The fake key exchange protocol is a 3-message protocol:
// - Client first sends ClientInit message to Server.
// - Server then sends ServerInitAndFinished message back to Client.
// - Client finally sends ClientFinished message to Server.
// This fake handshaker service is intended for ALTS integration testing without
// relying on real ALTS handshaker service inside GCE.
// It is thread-safe.
class FakeHandshakerService : public HandshakerService::Service {
public:
  explicit FakeHandshakerService(const std::string& peer_identity)
      : peer_identity_(peer_identity) {}

  grpc::Status
  DoHandshake(grpc::ServerContext* /*server_context*/,
              grpc::ServerReaderWriter<grpc::gcp::HandshakerResp, grpc::gcp::HandshakerReq>* stream)
      override {
    grpc::Status status;
    HandshakerContext context;
    grpc::gcp::HandshakerReq request;
    grpc::gcp::HandshakerResp response;
    while (stream->Read(&request)) {
      status = ProcessRequest(&context, request, &response);
      if (!status.ok())
        return WriteErrorResponse(stream, status);
      stream->Write(response);
      if (context.state == HandshakeState::COMPLETED)
        return grpc::Status::OK;
      request.Clear();
    }
    return grpc::Status::OK;
  }

private:
  // HandshakeState is used by fake handshaker server to keep track of client's
  // handshake status. In the beginning of a handshake, the state is INITIAL.
  // If start_client or start_server request is called, the state becomes at
  // least STARTED. When the handshaker server produces the first fame, the
  // state becomes SENT. After the handshaker server processes the final frame
  // from the peer, the state becomes COMPLETED.
  enum class HandshakeState { INITIAL, STARTED, SENT, COMPLETED };

  struct HandshakerContext {
    bool is_client = true;
    HandshakeState state = HandshakeState::INITIAL;
  };

  grpc::Status ProcessRequest(HandshakerContext* context, const grpc::gcp::HandshakerReq& request,
                              grpc::gcp::HandshakerResp* response) {
    response->Clear();
    if (request.has_client_start()) {
      return ProcessClientStart(context, request.client_start(), response);
    } else if (request.has_server_start()) {
      return ProcessServerStart(context, request.server_start(), response);
    } else if (request.has_next()) {
      return ProcessNext(context, request.next(), response);
    }
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Request is empty.");
  }

  grpc::Status ProcessClientStart(HandshakerContext* context,
                                  const grpc::gcp::StartClientHandshakeReq& request,
                                  grpc::gcp::HandshakerResp* response) {
    // Checks request.
    if (context->state != HandshakeState::INITIAL) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, kWrongStateError);
    }
    if (request.application_protocols_size() == 0) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "At least one application protocol needed.");
    }
    if (request.record_protocols_size() == 0) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "At least one record protocol needed.");
    }
    // Sets response.
    response->set_out_frames(kClientInitFrame);
    response->set_bytes_consumed(0);
    response->mutable_status()->set_code(grpc::StatusCode::OK);
    // Updates handshaker context.
    context->is_client = true;
    context->state = HandshakeState::SENT;
    return grpc::Status::OK;
  }

  grpc::Status ProcessServerStart(HandshakerContext* context,
                                  const grpc::gcp::StartServerHandshakeReq& request,
                                  grpc::gcp::HandshakerResp* response) {
    // Checks request.
    if (context->state != HandshakeState::INITIAL) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, kWrongStateError);
    }
    if (request.application_protocols_size() == 0) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "At least one application protocol needed.");
    }
    if (request.handshake_parameters().empty()) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "At least one set of handshake parameters needed.");
    }
    // Sets response.
    if (request.in_bytes().empty()) {
      // start_server request does not have in_bytes.
      response->set_bytes_consumed(0);
      context->state = HandshakeState::STARTED;
    } else {
      // start_server request has in_bytes.
      if (request.in_bytes() == kClientInitFrame) {
        response->set_out_frames(kServerFrame);
        response->set_bytes_consumed(strlen(kClientInitFrame));
        context->state = HandshakeState::SENT;
      } else {
        return grpc::Status(grpc::StatusCode::UNKNOWN, kInvalidFrameError);
      }
    }
    response->mutable_status()->set_code(grpc::StatusCode::OK);
    context->is_client = false;
    return grpc::Status::OK;
  }

  grpc::Status ProcessNext(HandshakerContext* context,
                           const grpc::gcp::NextHandshakeMessageReq& request,
                           grpc::gcp::HandshakerResp* response) {
    if (context->is_client) {
      // Processes next request on client side.
      if (context->state != HandshakeState::SENT) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, kWrongStateError);
      }
      if (request.in_bytes() != kServerFrame) {
        return grpc::Status(grpc::StatusCode::UNKNOWN, kInvalidFrameError);
      }
      response->set_out_frames(kClientFinishFrame);
      response->set_bytes_consumed(strlen(kServerFrame));
      context->state = HandshakeState::COMPLETED;
    } else {
      // Processes next request on server side.
      HandshakeState current_state = context->state;
      if (current_state == HandshakeState::STARTED) {
        if (request.in_bytes() != kClientInitFrame) {
          return grpc::Status(grpc::StatusCode::UNKNOWN, kInvalidFrameError);
        }
        response->set_out_frames(kServerFrame);
        response->set_bytes_consumed(strlen(kClientInitFrame));
        context->state = HandshakeState::SENT;
      } else if (current_state == HandshakeState::SENT) {
        // Client finish frame may be sent along with the first payload from the
        // client, handshaker only consumes the client finish frame.
        if (request.in_bytes().substr(0, strlen(kClientFinishFrame)) != kClientFinishFrame) {
          return grpc::Status(grpc::StatusCode::UNKNOWN, kInvalidFrameError);
        }
        response->set_bytes_consumed(strlen(kClientFinishFrame));
        context->state = HandshakeState::COMPLETED;
      } else {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, kWrongStateError);
      }
    }
    // At this point, processing next request succeeded.
    response->mutable_status()->set_code(grpc::StatusCode::OK);
    if (context->state == HandshakeState::COMPLETED) {
      *response->mutable_result() = GetHandshakerResult();
    }
    return grpc::Status::OK;
  }

  grpc::Status WriteErrorResponse(
      grpc::ServerReaderWriter<grpc::gcp::HandshakerResp, grpc::gcp::HandshakerReq>* stream,
      const grpc::Status& status) {
    EXPECT_TRUE(status.ok());
    grpc::gcp::HandshakerResp response;
    response.mutable_status()->set_code(status.error_code());
    response.mutable_status()->set_details(status.error_message());
    stream->Write(response);
    return status;
  }

  grpc::gcp::HandshakerResult GetHandshakerResult() {
    grpc::gcp::HandshakerResult result;
    result.set_application_protocol("grpc");
    result.set_record_protocol("ALTSRP_GCM_AES128_REKEY");
    result.mutable_peer_identity()->set_service_account(peer_identity_);
    result.mutable_local_identity()->set_service_account("local_identity");
    std::string key(1024, '\0');
    result.set_key_data(key);
    result.set_max_frame_size(16384);
    result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_major(2);
    result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_minor(1);
    result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_major(2);
    result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_minor(1);
    return result;
  }

  const std::string peer_identity_;
};

std::unique_ptr<Service> CreateFakeHandshakerService(const std::string& peer_identity) {
  return std::unique_ptr<Service>{new FakeHandshakerService(peer_identity)};
}

class AltsIntegrationTestBase : public Event::TestUsingSimulatedTime,
                                public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  AltsIntegrationTestBase(const std::string& server_peer_identity,
                          const std::string& client_peer_identity, bool server_connect_handshaker,
                          bool client_connect_handshaker, bool capturing_handshaker = false)
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()),
        server_peer_identity_(server_peer_identity), client_peer_identity_(client_peer_identity),
        server_connect_handshaker_(server_connect_handshaker),
        client_connect_handshaker_(client_connect_handshaker),
        capturing_handshaker_(capturing_handshaker) {}

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

    config_helper_.prependFilter(R"EOF(
    name: decode-dynamic-metadata-filter
    )EOF");

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

  void SetUp() override {
    fake_handshaker_server_thread_ = api_->threadFactory().createThread([this]() {
      std::unique_ptr<grpc::Service> service;
      if (capturing_handshaker_) {
        capturing_handshaker_service_ = new CapturingHandshakerService();
        service = std::unique_ptr<grpc::Service>{capturing_handshaker_service_};
      } else {
        capturing_handshaker_service_ = nullptr;
        service = CreateFakeHandshakerService("peer_identity");
      }

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
    class FakeSingletonManager : public Singleton::Manager {
    public:
      Singleton::InstanceSharedPtr get(const std::string&,
                                       Singleton::SingletonFactoryCb cb) override {
        return cb();
      }
    };
    FakeSingletonManager fsm;
    ON_CALL(mock_factory_ctx.server_context_, singletonManager()).WillByDefault(ReturnRef(fsm));
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
      fake_handshaker_server_->Shutdown(timeSystem().systemTime());
    }
    fake_handshaker_server_thread_->join();
  }

  Network::TransportSocketPtr makeAltsTransportSocket() {
    return client_alts_->createTransportSocket(/*options=*/nullptr,
                                               /*host=*/nullptr);
  }

  Network::ClientConnectionPtr makeAltsConnection() {
    auto client_transport_socket = makeAltsTransportSocket();
    Network::Address::InstanceConstSharedPtr address = getAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               std::move(client_transport_socket), nullptr,
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

  bool tsiPeerIdentitySet() {
    bool contain_peer_name = false;
    Http::TestRequestHeaderMapImpl upstream_request(upstream_request_->headers());
    upstream_request.iterate(
        [&contain_peer_name](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          const std::string key{header.key().getStringView()};
          const std::string value{header.value().getStringView()};
          if (key == "envoy.transport_sockets.peer_information.peer_identity" &&
              value == "peer_identity") {
            contain_peer_name = true;
          }
          return Http::HeaderMap::Iterate::Continue;
        });
    return contain_peer_name;
  }

  const std::string server_peer_identity_;
  const std::string client_peer_identity_;
  bool server_connect_handshaker_;
  bool client_connect_handshaker_;
  Thread::ThreadPtr fake_handshaker_server_thread_;
  std::unique_ptr<grpc::Server> fake_handshaker_server_;
  ConditionalInitializer fake_handshaker_server_ci_;
  int fake_handshaker_server_port_{};
  Network::UpstreamTransportSocketFactoryPtr client_alts_;
  TsiSocket* client_tsi_socket_{nullptr};
  bool capturing_handshaker_;
  CapturingHandshakerService* capturing_handshaker_service_;
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
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  EXPECT_TRUE(tsiPeerIdentitySet());
}

TEST_P(AltsIntegrationTestValidPeer, RouterRequestAndResponseWithBodyRawHttp) {
  autonomous_upstream_ = true;
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\n"
                                "Host: foo.com\r\n"
                                "Foo: bar\r\n"
                                "User-Agent: public\r\n"
                                "User-Agent: 123\r\n"
                                "Eep: baz\r\n\r\n",
                                &response, true, makeAltsTransportSocket());
  EXPECT_THAT(response, testing::StartsWith("HTTP/1.1 200 OK\r\n"));
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
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  EXPECT_FALSE(tsiPeerIdentitySet());
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
  codec_client_ = makeRawHttpConnection(makeAltsConnection(), absl::nullopt);
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
  codec_client_ = makeRawHttpConnection(makeAltsConnection(), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
}

class AltsIntegrationTestCapturingHandshaker : public AltsIntegrationTestBase {
public:
  AltsIntegrationTestCapturingHandshaker()
      : AltsIntegrationTestBase("", "",
                                /* server_connect_handshaker */ true,
                                /* client_connect_handshaker */ true,
                                /* capturing_handshaker */ true) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsIntegrationTestCapturingHandshaker,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that handshake request should include ALTS version.
TEST_P(AltsIntegrationTestCapturingHandshaker, CheckAltsVersion) {
  initialize();
  codec_client_ = makeRawHttpConnection(makeAltsConnection(), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
  EXPECT_EQ(capturing_handshaker_service_->client_versions.max_rpc_version().major(),
            capturing_handshaker_service_->server_versions.max_rpc_version().major());
  EXPECT_EQ(capturing_handshaker_service_->client_versions.max_rpc_version().minor(),
            capturing_handshaker_service_->server_versions.max_rpc_version().minor());
  EXPECT_EQ(capturing_handshaker_service_->client_versions.min_rpc_version().major(),
            capturing_handshaker_service_->server_versions.min_rpc_version().major());
  EXPECT_EQ(capturing_handshaker_service_->client_versions.min_rpc_version().minor(),
            capturing_handshaker_service_->server_versions.min_rpc_version().minor());
  EXPECT_NE(0, capturing_handshaker_service_->client_versions.max_rpc_version().major());
  EXPECT_NE(0, capturing_handshaker_service_->client_versions.max_rpc_version().minor());
  EXPECT_NE(0, capturing_handshaker_service_->client_versions.min_rpc_version().major());
  EXPECT_NE(0, capturing_handshaker_service_->client_versions.min_rpc_version().minor());
}

// Verifies that handshake request should include max frame size.
TEST_P(AltsIntegrationTestCapturingHandshaker, CheckMaxFrameSize) {
  initialize();
  codec_client_ = makeRawHttpConnection(makeAltsConnection(), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
  EXPECT_EQ(capturing_handshaker_service_->client_max_frame_size, 1024 * 1024);
  EXPECT_EQ(capturing_handshaker_service_->server_max_frame_size, 1024 * 1024);
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

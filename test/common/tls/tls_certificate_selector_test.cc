#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/ssl/ssl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/private_key/private_key_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/tls/ssl_handshaker.h"

#include "test/common/tls/cert_validator/timed_cert_validator.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using ::Envoy::StatusHelpers::IsOk;
using testing::_;
using testing::MockFunction;
using testing::NiceMock;
using ::testing::Not;
using testing::Ref;
using testing::ReturnRef;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

class TestTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                   public Ssl::UpstreamTlsCertificateSelector {
public:
  TestTlsCertificateSelector(Ssl::TlsCertificateSelectorContext& selector_ctx,
                             Ssl::SelectionResult::SelectionStatus mode,
                             OptRef<const Ssl::TlsContext> selected_context)
      : selector_ctx_(selector_ctx), mod_(mode), selected_context_(selected_context) {
    ENVOY_LOG_MISC(info, "debug: init provider");
  }

  ~TestTlsCertificateSelector() override {
    ENVOY_LOG_MISC(info, "debug: ~TestTlsCertificateSelector");
  }

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO&,
                                        Ssl::CertificateSelectionCallbackPtr cb) override {
    return selectTlsContext(std::move(cb));
  }

  Ssl::SelectionResult selectTlsContext(const SSL&,
                                        const Network::TransportSocketOptionsConstSharedPtr&,
                                        Ssl::CertificateSelectionCallbackPtr cb) override {
    return selectTlsContext(std::move(cb));
  }

  Ssl::SelectionResult selectTlsContext(Ssl::CertificateSelectionCallbackPtr cb) {
    ENVOY_LOG_MISC(info, "debug: select context");

    switch (mod_) {
    case Ssl::SelectionResult::SelectionStatus::Success:
      ENVOY_LOG_MISC(info, "debug: select cert done");
      return {mod_, &getTlsContext(), false};
      break;
    case Ssl::SelectionResult::SelectionStatus::Pending:
      ENVOY_LOG_MISC(info, "debug: select cert async");
      cb_ = std::move(cb);
      cb_->dispatcher().post([this] { selectTlsContextAsync(); });
      break;
    default:
      break;
    }
    return {mod_, nullptr, false};
  }

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("unreachable");
  }

  void selectTlsContextAsync() {
    ENVOY_LOG_MISC(info, "debug: select cert async done");
    cb_->onCertificateSelectionResult(getTlsContext(), false);
  }

  const Ssl::TlsContext& getTlsContext() {
    return selected_context_.has_value() ? *selected_context_ : selector_ctx_.getTlsContexts()[0];
  }

private:
  Ssl::TlsCertificateSelectorContext& selector_ctx_;
  const Ssl::SelectionResult::SelectionStatus mod_;
  const OptRef<const Ssl::TlsContext> selected_context_;
  Ssl::CertificateSelectionCallbackPtr cb_;
};

class TestTlsCertificateSelectorFactory;

class TestTlsSelectorFactory : public Ssl::TlsCertificateSelectorFactory,
                               public Ssl::UpstreamTlsCertificateSelectorFactory {
public:
  TestTlsSelectorFactory(TestTlsCertificateSelectorFactory& parent) : parent_(parent) {}
  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext& selector_ctx) override;
  Ssl::UpstreamTlsCertificateSelectorPtr
  createUpstreamTlsCertificateSelector(Ssl::TlsCertificateSelectorContext& selector_ctx) override;
  absl::Status onConfigUpdate() override { return absl::OkStatus(); }

private:
  TestTlsCertificateSelectorFactory& parent_;
};

class TestTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorConfigFactory,
                                          public Ssl::UpstreamTlsCertificateSelectorConfigFactory {
public:
  using CreateProviderHook =
      std::function<void(const Protobuf::Message&, Server::Configuration::GenericFactoryContext&)>;

  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig&, bool for_quic) override {
    if (selector_cb_) {
      selector_cb_(config, factory_context);
    }
    if (for_quic) {
      return absl::InvalidArgumentError("does not supported for quic");
    }
    return std::make_unique<TestTlsSelectorFactory>(*this);
  }
  absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
  createUpstreamTlsCertificateSelectorFactory(const Protobuf::Message&,
                                              Server::Configuration::GenericFactoryContext&,
                                              const Ssl::ClientContextConfig&) override {
    return std::make_unique<TestTlsSelectorFactory>(*this);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }
  std::string name() const override { return "test-tls-context-provider"; };

  CreateProviderHook selector_cb_;
  Ssl::SelectionResult::SelectionStatus mod_;
  OptRef<const Ssl::TlsContext> selected_context_;
};

Ssl::TlsCertificateSelectorPtr
TestTlsSelectorFactory::create(Ssl::TlsCertificateSelectorContext& selector_ctx) {
  return std::make_unique<TestTlsCertificateSelector>(selector_ctx, parent_.mod_,
                                                      parent_.selected_context_);
};

Ssl::UpstreamTlsCertificateSelectorPtr TestTlsSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext& selector_ctx) {
  return std::make_unique<TestTlsCertificateSelector>(selector_ctx, parent_.mod_,
                                                      parent_.selected_context_);
}

Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                    Network::TcpListenerCallbacks& cb, Runtime::Loader& runtime,
                                    const Network::ListenerConfig& listener_config,
                                    Server::ThreadLocalOverloadStateOptRef overload_state,
                                    Random::RandomGenerator& rng, Event::Dispatcher& dispatcher) {
  return std::make_unique<Network::TcpListenerImpl>(
      dispatcher, rng, runtime, std::move(socket), cb, listener_config.bindToPort(),
      listener_config.ignoreGlobalConnLimit(), listener_config.shouldBypassOverloadManager(),
      listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
}

using SelectionStatus = Ssl::SelectionResult::SelectionStatus;

struct TestParams {
  Network::Address::IpVersion ip_version;
  SelectionStatus mode;
  bool upstream;
};

std::string modeToString(SelectionStatus mode) {
  switch (mode) {
  case SelectionStatus::Success:
    return "Sync";
  case SelectionStatus::Pending:
    return "Async";
  default:
    return "Fail";
  }
}

std::string testParamsToString(const ::testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(p.param.ip_version),
                     modeToString(p.param.mode), p.param.upstream ? "Upstream" : "Downstream");
}

std::vector<SelectionStatus> getSelectionStatuses() {
  return {SelectionStatus::Success, SelectionStatus::Failed, SelectionStatus::Pending};
}

std::vector<TestParams> testParams() {
  std::vector<TestParams> ret;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto selection : getSelectionStatuses()) {
      ret.push_back(TestParams{ip_version, selection, true});
      ret.push_back(TestParams{ip_version, selection, false});
    }
  }
  return ret;
}

class TlsCertificateSelectorFactoryTest : public testing::TestWithParam<TestParams> {
protected:
  TlsCertificateSelectorFactoryTest()
      : registered_factory_(provider_factory_), upstream_registered_factory_(provider_factory_),
        version_(GetParam().ip_version) {}

  void runTest() {
    const auto mod = GetParam().mode;
    const bool upstream = GetParam().upstream;
    const std::string ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";
    const std::string selector_yaml = R"EOF(
    custom_tls_certificate_selector:
      name: test-tls-context-provider
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
    const std::string server_ctx_yaml = upstream ? ctx_yaml : absl::StrCat(ctx_yaml, selector_yaml);
    const std::string client_ctx_yaml = upstream ? absl::StrCat(ctx_yaml, selector_yaml) : ctx_yaml;

    NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
    Event::SimulatedTimeSystem time_system;
    Stats::TestUtil::TestStore server_stats_store(
        server_factory_context.serverScope().symbolTable());
    Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
    NiceMock<Runtime::MockLoader> runtime;
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
        transport_socket_factory_context;
    ON_CALL(transport_socket_factory_context.server_context_, api())
        .WillByDefault(ReturnRef(*server_api));

    MockFunction<TestTlsCertificateSelectorFactory::CreateProviderHook> mock_factory_cb;
    provider_factory_.selector_cb_ = mock_factory_cb.AsStdFunction();
    if (!upstream) {
      EXPECT_CALL(mock_factory_cb, Call)
          .WillOnce(WithArg<1>([&](Server::Configuration::GenericFactoryContext& context) {
            // Check that the objects available via the context are the same ones
            // provided to the parent context.
            EXPECT_THAT(context.serverFactoryContext().api(), Ref(*server_api));
          }));
    }

    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
    // provider factory callback will be Called here.
    auto server_cfg = *ServerContextConfigImpl::create(server_tls_context,
                                                       transport_socket_factory_context, {}, false);

    Event::DispatcherPtr dispatcher = server_api->allocateDispatcher("test_thread");
    provider_factory_.mod_ = mod;

    Tls::ContextManagerImpl manager(server_factory_context);
    auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
        std::move(server_cfg), manager, *server_stats_store.rootScope());

    auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(version_));
    Network::MockTcpListenerCallbacks callbacks;
    NiceMock<Network::MockListenerConfig> listener_config;
    Server::ThreadLocalOverloadStateOptRef overload_state;
    Network::ListenerPtr listener =
        createListener(socket, callbacks, runtime, listener_config, overload_state,
                       server_api->randomGenerator(), *dispatcher);

    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_tls_context);

    Stats::TestUtil::TestStore client_stats_store(
        server_factory_context.serverScope().symbolTable());
    Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system);
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
        client_factory_context;
    ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

    auto client_cfg = *ClientContextConfigImpl::create(client_tls_context, client_factory_context);
    auto client_ssl_socket_factory = *ClientSslSocketFactory::create(
        std::move(client_cfg), manager, *client_stats_store.rootScope());
    Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
        socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
        client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    Network::ConnectionPtr server_connection;
    Network::MockConnectionCallbacks server_connection_callbacks;
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    EXPECT_CALL(callbacks, onAccept_(_))
        .WillOnce([&](Network::ConnectionSocketPtr& socket) -> void {
          auto ssl_socket = server_ssl_socket_factory->createDownstreamTransportSocket();
          // configureInitialCongestionWindow is an unimplemented empty function, this is just to
          // increase code coverage.
          ssl_socket->configureInitialCongestionWindow(100, std::chrono::microseconds(123));
          server_connection = dispatcher->createServerConnection(
              std::move(socket), std::move(ssl_socket), stream_info);
          server_connection->addConnectionCallbacks(server_connection_callbacks);
        });
    EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

    Network::MockConnectionCallbacks client_connection_callbacks;
    client_connection->addConnectionCallbacks(client_connection_callbacks);
    client_connection->connect();

    size_t connect_count = 0;
    auto connect_second_time = [&]() {
      ENVOY_LOG_MISC(debug, "connect count: {}", connect_count);
      if (++connect_count == 2) {
        // By default, the session is not created with session resumption. With TLS 1.2, the client
        // sees a session ID but the server doesn't. TLS 1.3 has no session ID on the client. A TLS
        // 1.3 server has no session ID with BoringSSL, but OpenSSL creates one when it sends a
        // ticket.
        if (client_connection->ssl()->tlsVersion() == "TLSv1.3") {
          EXPECT_EQ(EMPTY_STRING, client_connection->ssl()->sessionId());
          EXPECT_EQ(SSL_SELECT(true, false), server_connection->ssl()->sessionId().empty());
        } else {
          EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->sessionId());
          EXPECT_NE(EMPTY_STRING, client_connection->ssl()->sessionId());
        }

        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher->exit();
      }
    };

    size_t close_count = 0;
    auto close_second_time = [&close_count, &dispatcher]() {
      if (++close_count == 2) {
        dispatcher->exit();
      }
    };

    if (mod == Ssl::SelectionResult::SelectionStatus::Failed) {
      EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
          .WillOnce([&](Network::ConnectionEvent) -> void { close_second_time(); });
      EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
          .WillOnce([&](Network::ConnectionEvent) -> void { close_second_time(); });
    } else {
      EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
          .WillOnce([&](Network::ConnectionEvent) -> void { connect_second_time(); });
      EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
          .WillOnce([&](Network::ConnectionEvent) -> void { connect_second_time(); });
      EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
      EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    }

    dispatcher->run(Event::Dispatcher::RunType::Block);
  }

  TestTlsCertificateSelectorFactory provider_factory_;
  Registry::InjectFactory<Ssl::TlsCertificateSelectorConfigFactory> registered_factory_;
  Registry::InjectFactory<Ssl::UpstreamTlsCertificateSelectorConfigFactory>
      upstream_registered_factory_;

  Network::Address::IpVersion version_;
};

TEST_P(TlsCertificateSelectorFactoryTest, Run) { runTest(); }

INSTANTIATE_TEST_SUITE_P(IpVersionsSelectorType, TlsCertificateSelectorFactoryTest,
                         testing::ValuesIn(testParams()), testParamsToString);

using TlsParameters = envoy::extensions::transport_sockets::tls::v3::TlsParameters;

class TlsCertificateSelectorSessionTicketTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, TlsParameters::TlsProtocol>> {
protected:
  TlsCertificateSelectorSessionTicketTest() : registered_factory_(provider_factory_) {}

  // Runs a handshake that starts on a TLS context with session ticket keys, which the selector
  // switches to a separately built TLS context, and returns whether the next connection of the
  // client offers a session.
  bool nextConnectionOffersSession(bool selected_context_has_keys) {
    const auto [ip_version, tls_version] = GetParam();
    // Stateful resumption is disabled, so a session can only resume from a ticket.
    const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    custom_tls_certificate_selector:
      name: test-tls-context-provider
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  disable_stateful_session_resumption: true
)EOF";
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext listener_tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), listener_tls_context);
    listener_tls_context.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(
        std::string(80, 'k'));
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext selected_tls_context =
        listener_tls_context;
    selected_tls_context.mutable_common_tls_context()->clear_custom_tls_certificate_selector();
    if (!selected_context_has_keys) {
      selected_tls_context.clear_session_ticket_keys();
    }

    NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
    Event::SimulatedTimeSystem time_system;
    Stats::TestUtil::TestStore stats_store(server_factory_context.serverScope().symbolTable());
    Api::ApiPtr api = Api::createApiForTest(stats_store, time_system);
    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
    ON_CALL(factory_context.server_context_, api()).WillByDefault(ReturnRef(*api));
    ContextManagerImpl manager(server_factory_context);

    auto selected_context_config = *ServerContextConfigImpl::create(
        selected_tls_context, factory_context, /*server_names=*/{}, /*for_quic=*/false);
    const std::unique_ptr<ServerContextImpl> selected_context = *ServerContextImpl::create(
        *stats_store.rootScope(), *selected_context_config, server_factory_context, nullptr);
    provider_factory_.mod_ = SelectionStatus::Success;
    provider_factory_.selected_context_ = makeOptRef(selected_context->getTlsContexts()[0]);
    auto listener_context_config = *ServerContextConfigImpl::create(
        listener_tls_context, factory_context, /*server_names=*/{}, /*for_quic=*/false);
    auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
        std::move(listener_context_config), manager, *stats_store.rootScope());

    Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
    auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(ip_version));
    Network::MockTcpListenerCallbacks listener_callbacks;
    NiceMock<Network::MockListenerConfig> listener_config;
    NiceMock<Runtime::MockLoader> runtime;
    Network::ListenerPtr listener = createListener(
        socket, listener_callbacks, runtime, listener_config,
        Server::ThreadLocalOverloadStateOptRef(), api->randomGenerator(), *dispatcher);

    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;
    auto* client_tls_params = client_tls_context.mutable_common_tls_context()->mutable_tls_params();
    client_tls_params->set_tls_minimum_protocol_version(tls_version);
    client_tls_params->set_tls_maximum_protocol_version(tls_version);
    auto client_config = *ClientContextConfigImpl::create(client_tls_context, factory_context);
    auto client_ssl_socket_factory = *ClientSslSocketFactory::create(
        std::move(client_config), manager, *stats_store.rootScope());
    Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
        socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
        client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    Network::MockConnectionCallbacks client_connection_callbacks;
    client_connection->addConnectionCallbacks(client_connection_callbacks);
    client_connection->connect();

    Network::ConnectionPtr server_connection;
    Network::MockConnectionCallbacks server_connection_callbacks;
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    EXPECT_CALL(listener_callbacks, onAccept_(_))
        .WillOnce([&](Network::ConnectionSocketPtr& accepted_socket) {
          server_connection = dispatcher->createServerConnection(
              std::move(accepted_socket),
              server_ssl_socket_factory->createDownstreamTransportSocket(), stream_info);
          server_connection->addConnectionCallbacks(server_connection_callbacks);
        });
    EXPECT_CALL(listener_callbacks, recordConnectionsAcceptedOnSocketEvent(_));

    // The server closes once both sides are connected, so that the client reads the TLS 1.3
    // session tickets, if any, before it sees the close.
    size_t connected_count = 0;
    const auto close_when_connected = [&](Network::ConnectionEvent) {
      if (++connected_count == 2) {
        server_connection->close(Network::ConnectionCloseType::NoFlush);
      }
    };
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(close_when_connected);
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(close_when_connected);
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce([&](Network::ConnectionEvent) { dispatcher->exit(); });
    dispatcher->run(Event::Dispatcher::RunType::Block);

    // A new transport socket of the client starts with the session that the client cached.
    const Network::TransportSocketPtr next_transport_socket =
        client_ssl_socket_factory->createTransportSocket(nullptr, nullptr);
    const SSL_SESSION* offered_session = SSL_get_session(
        dynamic_cast<const SslHandshakerImpl&>(*next_transport_socket->ssl()).ssl());
    return offered_session != nullptr && SSL_SESSION_is_resumable(offered_session);
  }

  TestTlsCertificateSelectorFactory provider_factory_;
  Registry::InjectFactory<Ssl::TlsCertificateSelectorConfigFactory> registered_factory_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsTlsVersions, TlsCertificateSelectorSessionTicketTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(TlsParameters::TLSv1_2, TlsParameters::TLSv1_3)),
    [](const testing::TestParamInfo<TlsCertificateSelectorSessionTicketTest::ParamType>& info) {
      return absl::StrCat(TestUtility::ipVersionToString(std::get<0>(info.param)), "_",
                          TlsParameters::TlsProtocol_Name(std::get<1>(info.param)));
    });

// A selector can switch a handshake from a TLS context with session ticket keys to one without
// them, which completes the handshake without a session ticket.
TEST_P(TlsCertificateSelectorSessionTicketTest, SelectedContextWithoutKeysSendsNoTicket) {
  EXPECT_FALSE(nextConnectionOffersSession(/*selected_context_has_keys=*/false));
}

TEST_P(TlsCertificateSelectorSessionTicketTest, SelectedContextWithKeysSendsTicket) {
  EXPECT_TRUE(nextConnectionOffersSession(/*selected_context_has_keys=*/true));
}

TEST(TlsCertificateSelectorFactoryQuicTest, QUICFactory) {
  TestTlsCertificateSelectorFactory provider_factory;
  Registry::InjectFactory<Ssl::TlsCertificateSelectorConfigFactory> registered_factory(
      provider_factory);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
    custom_tls_certificate_selector:
      name: test-tls-context-provider
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";

  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  // provider factory callback will be Called here.
  auto server_cfg = ServerContextConfigImpl::create(server_tls_context,
                                                    transport_socket_factory_context, {}, true);

  EXPECT_THAT(server_cfg, Not(IsOk()));
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

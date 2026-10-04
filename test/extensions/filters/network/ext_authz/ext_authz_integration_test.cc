#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/ssl/ssl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace {

using testing::Ge;
using testing::HasSubstr;

class ExtAuthzNetworkIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                       public BaseIntegrationTest {
public:
  ExtAuthzNetworkIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeTest(bool send_tls_alert_on_denial, bool with_tls, bool shadow_mode = false) {
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz");
      ConfigHelper::setHttp2(*ext_authz_cluster);
    });

    config_helper_.addConfigModifier([this, send_tls_alert_on_denial, with_tls, shadow_mode](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);

      envoy::extensions::filters::network::ext_authz::v3::ExtAuthz ext_authz_config;
      ext_authz_config.set_stat_prefix("ext_authz");
      setGrpcService(*ext_authz_config.mutable_grpc_service(), "ext_authz",
                     fake_upstreams_.back()->localAddress());
      ext_authz_config.set_send_tls_alert_on_denial(send_tls_alert_on_denial);
      ext_authz_config.set_shadow_mode(shadow_mode);

      // Save the existing tcp_proxy filter config.
      auto tcp_proxy_filter = filter_chain->filters(0);

      // Clear and rebuild with ext_authz first, then tcp_proxy.
      filter_chain->clear_filters();

      auto* ext_authz_filter = filter_chain->add_filters();
      ext_authz_filter->set_name("envoy.filters.network.ext_authz");
      std::ignore = ext_authz_filter->mutable_typed_config()->PackFrom(ext_authz_config);

      filter_chain->add_filters()->CopyFrom(tcp_proxy_filter);

      // Configure TLS if requested.
      if (with_tls) {
        envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
        const std::string rundir = TestEnvironment::runfilesDirectory();

        auto* common_tls_context = tls_context.mutable_common_tls_context();
        common_tls_context->add_alpn_protocols("h2");
        common_tls_context->add_alpn_protocols("http/1.1");

        auto* validation_context = common_tls_context->mutable_validation_context();
        validation_context->mutable_trusted_ca()->set_filename(
            rundir + "/test/config/integration/certs/cacert.pem");

        auto* tls_certificate = common_tls_context->add_tls_certificates();
        tls_certificate->mutable_certificate_chain()->set_filename(
            rundir + "/test/config/integration/certs/servercert.pem");
        tls_certificate->mutable_private_key()->set_filename(
            rundir + "/test/config/integration/certs/serverkey.pem");

        auto* transport_socket = filter_chain->mutable_transport_socket();
        transport_socket->set_name("envoy.transport_sockets.tls");
        std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
      }
    });

    BaseIntegrationTest::initialize();

    if (with_tls) {
      context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
          server_factory_context_);
    }
  }

  void setupSslConnection() {
    payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_,
                                                          &server_factory_context_.serverScope());
    ssl_client_ = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    ssl_client_->addConnectionCallbacks(connect_callbacks_);
    ssl_client_->addReadFilter(payload_reader_);
    ssl_client_->connect();

    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForExtAuthzConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForExtAuthzRequest() {
    return fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
  }

  void sendExtAuthzResponse(Grpc::Status::GrpcStatus check_status) {
    ext_authz_request_->startGrpcStream();

    envoy::service::auth::v3::CheckResponse response;
    if (check_status == Grpc::Status::WellKnownGrpcStatus::Ok) {
      response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
    } else {
      response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
    }

    ext_authz_request_->sendGrpcMessage(response);
    // The gRPC call itself always succeeds. The denial is in the CheckResponse.
    ext_authz_request_->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  }

  void cleanupExtAuthzConnection() {
    if (fake_ext_authz_connection_ != nullptr) {
      AssertionResult result = fake_ext_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_ext_authz_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_ext_authz_connection_ = nullptr;
    }
  }

  std::unique_ptr<Extensions::TransportSockets::Tls::ContextManagerImpl> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  Network::ClientConnectionPtr ssl_client_;
  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzNetworkIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test that when ext_authz denies with TLS and send_tls_alert_on_denial is true,
// the connection is closed with a TLS alert.
BORINGSSL_TEST_P(ExtAuthzNetworkIntegrationTest, DenialWithTlsAlertEnabled) {
  initializeTest(true /* send_tls_alert_on_denial */, true /* with_tls */);

  setupSslConnection();
  ASSERT_TRUE(connect_callbacks_.connected());

  // After connection is established, we can access the SSL object to check for alerts.
  // We'll verify the alert was sent by checking the transport failure reason after closure.
  Buffer::OwnedImpl data("some_data");
  ssl_client_->write(data, false);

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  test_server_->waitForCounter("ext_authz.ext_authz.denied", Ge(1));
  test_server_->waitForCounter("ext_authz.ext_authz.cx_closed", Ge(1));

  // Wait for the connection to close and ensure all events are processed.
  while (!connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Run the dispatcher one more time to ensure the transport failure reason is set.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_TRUE(connect_callbacks_.closed());

  // When send_tls_alert_on_denial is true, the server sends a TLS access_denied(49) alert.
  // The client's SSL library processes this alert, and it should be reflected in the
  // transport failure reason. The alert causes the connection to be closed with an SSL error.
  // Access the failure reason on the dispatcher thread to avoid data races.
  std::string failure_reason;
  dispatcher_->post([this, &failure_reason]() {
    failure_reason = std::string(ssl_client_->transportFailureReason());
  });
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // The failure reason should indicate an SSL/TLS error occurred, typically containing
  // information about the alert received. With BoringSSL, when an access_denied alert
  // is received, it results in a connection closure with SSL error information.
  EXPECT_NE(failure_reason, "") << "Expected transport failure reason to be set due to TLS alert";

  // Additionally, when an alert is received, BoringSSL typically logs it and may include
  // it in the failure reason. The exact format may vary, but we expect it to have some
  // indication that the SSL error occurred during connection closure and has the Access
  // Denied as the alert description.
  EXPECT_THAT(failure_reason, HasSubstr("ACCESS_DENIED"))
      << "Expected failure reason to indicate ACCESS_DENIED error: " << failure_reason;

  ssl_client_->close(Network::ConnectionCloseType::NoFlush);

  cleanupExtAuthzConnection();
}

// Test that when ext_authz denies with TLS and send_tls_alert_on_denial is false,
// the connection is still closed without the alert.
TEST_P(ExtAuthzNetworkIntegrationTest, DenialWithTlsAlertDisabled) {
  initializeTest(false /* send_tls_alert_on_denial */, true /* with_tls */);

  setupSslConnection();
  ASSERT_TRUE(connect_callbacks_.connected());

  Buffer::OwnedImpl data("some_data");
  ssl_client_->write(data, false);

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  test_server_->waitForCounter("ext_authz.ext_authz.denied", Ge(1));
  test_server_->waitForCounter("ext_authz.ext_authz.cx_closed", Ge(1));

  while (!connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Run the dispatcher one more time to ensure all events are processed.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_TRUE(connect_callbacks_.closed());

  // When send_tls_alert_on_denial is false, the connection is closed without sending an alert.
  // This results in a different failure pattern. The connection is just closed without the
  // SSL library receiving an alert, so the transport failure reason should be empty or should
  // indicate a different type of closure (not an SSL error).
  // Access the failure reason on the dispatcher thread to avoid data races.
  std::string failure_reason;
  dispatcher_->post([this, &failure_reason]() {
    failure_reason = std::string(ssl_client_->transportFailureReason());
  });
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // When no alert is sent, the connection is closed at the TCP level without SSL errors,
  // so we expect either no failure reason or a non-TLS related reason.
  EXPECT_EQ(failure_reason, "")
      << "Expected no transport failure reason when TLS alert is disabled, got: " << failure_reason;

  ssl_client_->close(Network::ConnectionCloseType::NoFlush);

  cleanupExtAuthzConnection();
}

// Test that when ext_authz allows the connection, it proceeds to tcp_proxy.
TEST_P(ExtAuthzNetworkIntegrationTest, AllowedConnection) {
  initializeTest(true /* send_tls_alert_on_denial */, true /* with_tls */);

  setupSslConnection();
  ASSERT_TRUE(connect_callbacks_.connected());

  Buffer::OwnedImpl data("some_data");
  ssl_client_->write(data, false);

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::Ok);

  test_server_->waitForCounter("ext_authz.ext_authz.ok", Ge(1));

  FakeRawConnectionPtr fake_upstream_connection;
  result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection);
  RELEASE_ASSERT(result, result.message());

  result = fake_upstream_connection->waitForData(9);
  RELEASE_ASSERT(result, result.message());

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  payload_reader_->setDataToWaitFor("world");
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  while (!connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ssl_client_->close(Network::ConnectionCloseType::NoFlush);

  EXPECT_EQ("world", payload_reader_->data());

  cleanupExtAuthzConnection();
}

// Test that denial works without TLS. No alert sent, but connection still closes.
TEST_P(ExtAuthzNetworkIntegrationTest, DenialWithoutTls) {
  initializeTest(true /* send_tls_alert_on_denial */, false /* with_tls */);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("some_data", false, false));

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  // Wait for denial to be processed.
  test_server_->waitForCounter("ext_authz.ext_authz.denied", Ge(1));
  test_server_->waitForCounter("ext_authz.ext_authz.cx_closed", Ge(1));

  // For non-TLS connections, ext_authz closes immediately without sending an alert.
  // Close the client connection to clean up the test.
  tcp_client->close();

  cleanupExtAuthzConnection();
}

// Test that in shadow mode a denial does not close the connection. The data reaches the upstream
// and the decision is readable from filter state.
TEST_P(ExtAuthzNetworkIntegrationTest, ShadowModeDenialDoesNotCloseConnection) {
  useListenerAccessLog("%FILTER_STATE(envoy.filters.network.ext_authz:PLAIN)% "
                       "field=%FILTER_STATE(envoy.filters.network.ext_authz:FIELD:check_result)%");
  initializeTest(false /* send_tls_alert_on_denial */, false /* with_tls */,
                 true /* shadow_mode */);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("some_data", false, false));

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  test_server_->waitForCounter("ext_authz.ext_authz.denied", Ge(1));

  // The connection stays open, so the buffered data is released to the tcp_proxy filter and
  // reaches the upstream.
  FakeRawConnectionPtr fake_upstream_connection;
  result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection->waitForData(9);
  RELEASE_ASSERT(result, result.message());

  EXPECT_EQ(0, test_server_->counter("ext_authz.ext_authz.cx_closed")->value());
  EXPECT_EQ(0, test_server_->counter("ext_authz.ext_authz.ok")->value());

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
  cleanupExtAuthzConnection();
  test_server_.reset();

  const std::string log = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log, HasSubstr("\"check_result\":\"DENIED\""));
  EXPECT_THAT(log, HasSubstr("\"status_code\":403"));
  EXPECT_THAT(log, HasSubstr("field=DENIED"));
}

// Test that in shadow mode no TLS alert is sent on a denial and the TLS connection remains usable.
TEST_P(ExtAuthzNetworkIntegrationTest, ShadowModeDenialSendsNoTlsAlert) {
  initializeTest(true /* send_tls_alert_on_denial */, true /* with_tls */, true /* shadow_mode */);

  setupSslConnection();
  ASSERT_TRUE(connect_callbacks_.connected());

  Buffer::OwnedImpl data("some_data");
  ssl_client_->write(data, false);

  AssertionResult result = waitForExtAuthzConnection();
  RELEASE_ASSERT(result, result.message());
  result = waitForExtAuthzRequest();
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  sendExtAuthzResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  test_server_->waitForCounter("ext_authz.ext_authz.denied", Ge(1));

  // The connection survives the denial and still proxies in both directions.
  FakeRawConnectionPtr fake_upstream_connection;
  result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection->waitForData(9);
  RELEASE_ASSERT(result, result.message());

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  payload_reader_->setDataToWaitFor("world");
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ("world", payload_reader_->data());
  EXPECT_FALSE(connect_callbacks_.closed());

  EXPECT_EQ(0, test_server_->counter("ext_authz.ext_authz.cx_closed")->value());

  // No access_denied alert was sent, so the client saw no TLS error. Access the failure reason on
  // the dispatcher thread to avoid data races.
  std::string failure_reason;
  dispatcher_->post([this, &failure_reason]() {
    failure_reason = std::string(ssl_client_->transportFailureReason());
  });
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(failure_reason, "") << "Expected no transport failure reason in shadow mode, got: "
                                << failure_reason;

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ssl_client_->close(Network::ConnectionCloseType::NoFlush);
  cleanupExtAuthzConnection();
}

} // namespace
} // namespace Envoy

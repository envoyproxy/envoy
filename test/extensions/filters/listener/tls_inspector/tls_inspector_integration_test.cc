#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TlsInspectorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {
public:
  TlsInspectorIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      filters:
       -  name: envoy.filters.network.echo
)EOF") {}

  ~TlsInspectorIntegrationTest() override = default;
  std::string appendMatcher(const std::string& listener_filter, bool disabled) {
    if (disabled) {
      return listener_filter +
             R"EOF(
filter_disabled:
  any_match: true
)EOF";
    } else {
      return listener_filter +
             R"EOF(
filter_disabled:
  not_match:
    any_match: true
)EOF";
    }
  }

  void initializeWithTlsInspector(bool ssl_client, const std::string& log_format,
                                  absl::optional<bool> listener_filter_disabled = absl::nullopt,
                                  bool enable_ja3_fingerprinting = false) {
    config_helper_.renameListener("echo");
    std::string tls_inspector_config = ConfigHelper::tlsInspectorFilter(enable_ja3_fingerprinting);
    if (listener_filter_disabled.has_value()) {
      tls_inspector_config = appendMatcher(tls_inspector_config, listener_filter_disabled.value());
    }
    config_helper_.addListenerFilter(tls_inspector_config);

    config_helper_.addConfigModifier([ssl_client](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      if (ssl_client) {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        auto* alpn = filter_chain->mutable_filter_chain_match()->add_application_protocols();
        *alpn = "envoyalpn";
      }
      auto* timeout = bootstrap.mutable_static_resources()
                          ->mutable_listeners(0)
                          ->mutable_listener_filters_timeout();
      timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
      bootstrap.mutable_static_resources()
          ->mutable_listeners(0)
          ->set_continue_on_listener_filters_timeout(true);
    });
    if (ssl_client) {
      config_helper_.addSslConfig();
    }

    useListenerAccessLog(log_format);
    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  }

  void setupConnections(bool listener_filter_disabled, bool expect_connection_open, bool ssl_client,
                        const std::string& log_format = "%RESPONSE_CODE_DETAILS%",
                        const Ssl::ClientSslTransportOptions& ssl_options = {},
                        const std::string& curves_list = "",
                        bool enable_ja3_fingerprinting = false) {
    initializeWithTlsInspector(ssl_client, log_format, listener_filter_disabled,
                               enable_ja3_fingerprinting);

    // Set up the SSL client.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("echo"));
    context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
    Network::TransportSocketPtr transport_socket;
    if (ssl_client) {
      transport_socket =
          context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
                                              absl::string_view(""), std::vector<std::string>(),
                                              std::vector<std::string>{"envoyalpn"}),
                                          nullptr);

      if (!curves_list.empty()) {
        auto ssl_socket =
            dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
        ASSERT(ssl_socket != nullptr);
        SSL_set1_curves_list(ssl_socket->rawSslForTest(), curves_list.c_str());
      }
    } else {
      auto transport_socket_factory = std::make_unique<Network::RawBufferSocketFactory>();
      transport_socket = transport_socket_factory->createTransportSocket(nullptr, nullptr);
    }
    client_ =
        dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                            std::move(transport_socket), nullptr, nullptr);
    client_->addConnectionCallbacks(connect_callbacks_);
    client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

    if (expect_connection_open) {
      ASSERT(connect_callbacks_.connected());
      ASSERT_FALSE(connect_callbacks_.closed());
    } else {
      ASSERT_FALSE(connect_callbacks_.connected());
      ASSERT(connect_callbacks_.closed());
    }
  }

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr client_;
};

// Each listener filter is enabled by default.
TEST_P(TlsInspectorIntegrationTest, AllListenerFiltersAreEnabledByDefault) {
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/true);
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::Eq("-"));
}

// The tls_inspector is disabled. The ALPN won't be sniffed out and no filter chain is matched.
TEST_P(TlsInspectorIntegrationTest, DisabledTlsInspectorFailsFilterChainFind) {
  setupConnections(/*listener_filter_disabled=*/true, /*expect_connection_open=*/false,
                   /*ssl_client=*/true);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Eq(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound));
}

// trigger the tls inspect filter timeout, and continue create new connection after timeout
TEST_P(TlsInspectorIntegrationTest, ContinueOnListenerTimeout) {
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/false);
  // The length of tls hello message is defined as `TLS_MAX_CLIENT_HELLO = 64 * 1024`
  // if tls inspect filter doesn't read the max length of hello message data, it
  // will continue wait. Then the listener filter timeout timer will be triggered.
  Buffer::OwnedImpl buffer("fake data");
  client_->write(buffer, false);
  // The timeout is set as one seconds, advance 2 seconds to trigger the timeout.
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(2000));
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::Eq("-"));
}

// The `JA3` fingerprint is correct in the access log.
TEST_P(TlsInspectorIntegrationTest, JA3FingerprintIsSet) {
  // These TLS options will create a client hello message with
  // `JA3` fingerprint:
  //   `771,49199,23-65281-10-11-35-16-13,23,0`
  // MD5 hash:
  //   `71d1f47d1125ac53c3c6a4863c087cfe`
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/true, /*log_format=*/"%TLS_JA3_FINGERPRINT%",
                   /*ssl_options=*/ssl_options, /*curves_list=*/"P-256",
                   /*enable_`ja3`_fingerprinting=*/true);
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Eq("71d1f47d1125ac53c3c6a4863c087cfe"));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, TlsInspectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace
} // namespace Envoy

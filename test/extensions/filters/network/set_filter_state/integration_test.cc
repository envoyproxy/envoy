#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/router/string_accessor_impl.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SetFilterState {

class ObjectFooFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "foo"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ObjectFooFactory, StreamInfo::FilterState::ObjectFactory);

class SetFilterStateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,

                                      public BaseIntegrationTest {
public:
  SetFilterStateIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.set_filter_state
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
          on_new_connection:
          - object_key: early
            factory_key: foo
            format_string:
              text_format_source:
                inline_string: "bar"
          on_downstream_tls_handshake:
          - object_key: late
            factory_key: foo
            format_string:
              text_format_source:
                inline_string: "baz"
      - name: envoy.filters.network.echo
        typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
      )EOF");
  }

  void SetUp() override {
    useListenerAccessLog("%FILTER_STATE(early)%|%FILTER_STATE(late)%");
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetFilterStateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SetFilterStateIntegrationTest, PlaintextConnectionAppliesBothLifecycleLists) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::HasSubstr("\"bar\"|\"baz\""));
}

class SetFilterStateDownstreamTlsIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  SetFilterStateDownstreamTlsIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.set_filter_state
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
          on_new_connection:
          - object_key: early
            factory_key: foo
            format_string:
              text_format_source:
                inline_string: "early-%DOWNSTREAM_PEER_URI_SAN%"
          on_downstream_tls_handshake:
          - object_key: late
            factory_key: foo
            format_string:
              text_format_source:
                inline_string: "late-%DOWNSTREAM_PEER_URI_SAN%"
      - name: envoy.filters.network.echo
        typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
      )EOF");
  }

  void SetUp() override {
    useListenerAccessLog("%FILTER_STATE(early)%|%FILTER_STATE(late)%");

    // Enable downstream TLS and require a client certificate so that peer SANs exist.
    config_helper_.addSslConfig();
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      const bool unpack_ok =
          filter_chain->mutable_transport_socket()->mutable_typed_config()->UnpackTo(&tls_context);
      RELEASE_ASSERT(unpack_ok, "failed to unpack DownstreamTlsContext for test listener");
      tls_context.mutable_require_client_certificate()->set_value(true);
      filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    });

    BaseIntegrationTest::initialize();
  }

  Network::ClientConnectionPtr makeTlsClientConnection() {
    auto client_transport_socket_factory =
        Ssl::createClientSslTransportSocketFactory(/*options=*/{}, context_manager_, *api_);
    auto address = Ssl::getSslAddress(version_, lookupPort("listener_0"));
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory->createTransportSocket({}, nullptr), nullptr, nullptr);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetFilterStateDownstreamTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SetFilterStateDownstreamTlsIntegrationTest, TlsConnectionAppliesTlsListAfterHandshake) {
  ConnectionStatusCallbacks connect_callbacks;
  auto ssl_client = makeTlsClientConnection();
  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->connect();

  while (!connect_callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ssl_client->close(Network::ConnectionCloseType::NoFlush);
  while (!connect_callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  const std::string log_entry =
      waitForAccessLog(listener_access_log_name_, /*entry=*/0, /*allow_excess_entries=*/false,
                       /*client_connection=*/ssl_client.get());

  // The peer certificate SANs are not available until the TLS handshake completes. The
  // on_new_connection hook runs before that, so the early value must not contain the URI SAN,
  // while the on_downstream_tls_handshake value must contain it.
  EXPECT_THAT(log_entry, testing::HasSubstr("\"early-"));
  EXPECT_THAT(log_entry, testing::Not(testing::HasSubstr("early-spiffe://lyft.com/frontend-team")));
  EXPECT_THAT(log_entry, testing::HasSubstr("\"late-spiffe://lyft.com/frontend-team"));
}

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

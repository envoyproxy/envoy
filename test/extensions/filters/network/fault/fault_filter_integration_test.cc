#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {
namespace {

constexpr absl::string_view kFaultAccessLogFormat =
    "%RESPONSE_FLAGS%|%RESPONSE_CODE_DETAILS%|%CONNECTION_TERMINATION_DETAILS%|"
    "%DOWNSTREAM_LOCAL_CLOSE_REASON%|%DYNAMIC_METADATA(envoy.filters.network.fault)%";

class NetworkFaultFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkFaultFilterIntegrationTest() : BaseIntegrationTest(GetParam(), baseFilterChainConfig()) {}

  static std::string baseFilterChainConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.fault
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
          stat_prefix: base_test
      - name: envoy.filters.network.echo
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF");
  }

  void initializeWithFaultFilter(const std::string& fault_filter_yaml) {
    useListenerAccessLog(kFaultAccessLogFormat);
    config_helper_.addConfigModifier(
        [fault_filter_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(fault_filter_yaml, filter);
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->mutable_filter_chains(0)->mutable_filters(0)->Swap(&filter);
        });
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkFaultFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(NetworkFaultFilterIntegrationTest, EnforcedAbortClosesDownstreamConnection) {
  initializeWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();

  test_server_->waitForCounter("fault.tcp_test.aborts_injected", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_dry_run")->value());

  const std::string log_entry = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_entry, HasSubstr("FI|fault_filter_abort|fault_filter_abort|fault_filter_abort|"));
  EXPECT_THAT(log_entry, HasSubstr("\"experiment_name\":\"test_experiment\""));
  EXPECT_THAT(log_entry, HasSubstr("\"fault_name\":\"fault0\""));
}

TEST_P(NetworkFaultFilterIntegrationTest, DryRunAbortAllowsTrafficAndRecordsMetadata) {
  initializeWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    dry_run: true
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  test_server_->waitForCounter("fault.tcp_test.aborts_dry_run", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_injected")->value());

  const std::string log_entry = waitForAccessLog(listener_access_log_name_);
  // Response flags must not contain FI in dry_run mode, and dynamic metadata must contain the
  // dry_run_abort marker instead of user-configured filter_metadata.
  EXPECT_THAT(log_entry, HasSubstr("-|-|-|-|"));
  EXPECT_THAT(log_entry, HasSubstr("\"dry_run_abort\":true"));
  EXPECT_THAT(log_entry, Not(HasSubstr("experiment_name")));
  EXPECT_THAT(log_entry, Not(HasSubstr("fault_name")));
}

TEST_P(NetworkFaultFilterIntegrationTest, ZeroPercentAbortAllowsTrafficWithoutMetadata) {
  initializeWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_test
  abort:
    percentage:
      numerator: 0
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  const std::string log_entry = waitForAccessLog(listener_access_log_name_);
  EXPECT_EQ("-|-|-|-|-", log_entry);
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_injected")->value());
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_dry_run")->value());
}

TEST_P(NetworkFaultFilterIntegrationTest, RuntimeOverrideDisablesEnforcedAbort) {
  config_helper_.addRuntimeOverride("fault.network.abort.abort_percent", "0");
  initializeWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  tcp_client->waitForData("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  const std::string log_entry = waitForAccessLog(listener_access_log_name_);
  EXPECT_EQ("-|-|-|-|-", log_entry);
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_injected")->value());
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_dry_run")->value());
}

TEST_P(NetworkFaultFilterIntegrationTest, CustomRuntimeKeyEnablesAbort) {
  config_helper_.addRuntimeOverride("custom.network.fault.abort_percent", "100");
  initializeWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_test
  abort_percent_runtime: custom.network.fault.abort_percent
  abort:
    percentage:
      numerator: 0
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();

  test_server_->waitForCounter("fault.tcp_test.aborts_injected", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_test.aborts_dry_run")->value());

  const std::string log_entry = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_entry, HasSubstr("FI|fault_filter_abort|fault_filter_abort|fault_filter_abort|"));
  EXPECT_THAT(log_entry, HasSubstr("\"experiment_name\":\"test_experiment\""));
}

class NetworkFaultTcpProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkFaultTcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void initializeWithFaultBeforeTcpProxy(const std::string& fault_filter_yaml) {
    tcp_proxy_access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
    const std::string access_log_path = tcp_proxy_access_log_name_;
    config_helper_.addConfigModifier(
        [fault_filter_yaml, access_log_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          ASSERT_EQ(1, filter_chain->filters_size());

          // Attach a file access log directly to tcp_proxy to verify tcp_proxy logs fault metadata
          // and response flags even when the fault filter aborts in onNewConnection().
          envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy_config;
          ASSERT_TRUE(filter_chain->filters(0).typed_config().UnpackTo(&tcp_proxy_config));
          envoy::extensions::access_loggers::file::v3::FileAccessLog file_log;
          file_log.set_path(access_log_path);
          file_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
              absl::StrCat(kFaultAccessLogFormat, "\n"));
          auto* access_log = tcp_proxy_config.add_access_log();
          access_log->set_name("envoy.access_loggers.file");
          ASSERT_TRUE(access_log->mutable_typed_config()->PackFrom(file_log));
          ASSERT_TRUE(
              filter_chain->mutable_filters(0)->mutable_typed_config()->PackFrom(tcp_proxy_config));

          // Prepend the network fault filter ahead of tcp_proxy.
          envoy::config::listener::v3::Filter fault_filter;
          TestUtility::loadFromYaml(fault_filter_yaml, fault_filter);
          envoy::config::listener::v3::Filter existing_tcp_proxy = filter_chain->filters(0);
          filter_chain->clear_filters();
          *filter_chain->add_filters() = fault_filter;
          *filter_chain->add_filters() = existing_tcp_proxy;
        });
    BaseIntegrationTest::initialize();
  }

  std::string tcp_proxy_access_log_name_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkFaultTcpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(NetworkFaultTcpProxyIntegrationTest, EnforcedAbortSkipsUpstreamAndFlushesTcpProxyAccessLog) {
  initializeWithFaultBeforeTcpProxy(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_proxy_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();

  test_server_->waitForCounter("fault.tcp_proxy_test.aborts_injected", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_proxy_test.aborts_dry_run")->value());

  const std::string log_entry = waitForAccessLog(tcp_proxy_access_log_name_);
  EXPECT_THAT(log_entry, HasSubstr("FI|fault_filter_abort|fault_filter_abort|fault_filter_abort|"));
  EXPECT_THAT(log_entry, HasSubstr("\"experiment_name\":\"test_experiment\""));
  EXPECT_THAT(log_entry, HasSubstr("\"fault_name\":\"fault0\""));
  // No upstream connection should be attempted when the fault filter aborts in onNewConnection().
  EXPECT_EQ(0U, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
}

TEST_P(NetworkFaultTcpProxyIntegrationTest, DryRunAbortProxiesToUpstreamAndLogsMetadata) {
  initializeWithFaultBeforeTcpProxy(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tcp_proxy_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    dry_run: true
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounter("fault.tcp_proxy_test.aborts_dry_run", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tcp_proxy_test.aborts_injected")->value());
  EXPECT_EQ(1U, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  const std::string log_entry = waitForAccessLog(tcp_proxy_access_log_name_);
  EXPECT_THAT(log_entry, HasSubstr("-|-|-|-|"));
  EXPECT_THAT(log_entry, HasSubstr("\"dry_run_abort\":true"));
  EXPECT_THAT(log_entry, Not(HasSubstr("experiment_name")));
  EXPECT_THAT(log_entry, Not(HasSubstr("fault_name")));
}

class NetworkFaultDownstreamTlsIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkFaultDownstreamTlsIntegrationTest()
      : BaseIntegrationTest(GetParam(),
                            NetworkFaultFilterIntegrationTest::baseFilterChainConfig()) {}

  void initializeTlsWithFaultFilter(const std::string& fault_filter_yaml) {
    useListenerAccessLog(kFaultAccessLogFormat);
    config_helper_.addSslConfig();
    config_helper_.addConfigModifier(
        [fault_filter_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(fault_filter_yaml, filter);
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->mutable_filter_chains(0)->mutable_filters(0)->Swap(&filter);
        });
    BaseIntegrationTest::initialize();
  }

  Network::ClientConnectionPtr makeTlsClientConnection() {
    client_transport_socket_factory_ = Ssl::createClientSslTransportSocketFactory(
        /*options=*/{}, context_manager_, *api_, &server_factory_context_.serverScope());
    auto address = Ssl::getSslAddress(version_, lookupPort("listener_0"));
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory_->createTransportSocket({}, nullptr), nullptr, nullptr);
  }

  Network::UpstreamTransportSocketFactoryPtr client_transport_socket_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkFaultDownstreamTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(NetworkFaultDownstreamTlsIntegrationTest, EnforcedAbortClosesDownstreamTlsConnection) {
  initializeTlsWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tls_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  ConnectionStatusCallbacks connect_callbacks;
  auto ssl_client = makeTlsClientConnection();
  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->connect();

  while (!connect_callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  test_server_->waitForCounter("fault.tls_test.aborts_injected", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tls_test.aborts_dry_run")->value());

  const std::string log_entry =
      waitForAccessLog(listener_access_log_name_, /*entry=*/0, /*allow_excess_entries=*/false,
                       /*client_connection=*/ssl_client.get());
  EXPECT_THAT(log_entry, HasSubstr("FI|fault_filter_abort|fault_filter_abort|fault_filter_abort|"));
  EXPECT_THAT(log_entry, HasSubstr("\"experiment_name\":\"test_experiment\""));
  EXPECT_THAT(log_entry, HasSubstr("\"fault_name\":\"fault0\""));
}

TEST_P(NetworkFaultDownstreamTlsIntegrationTest, DryRunAbortAllowsDownstreamTlsConnection) {
  initializeTlsWithFaultFilter(R"EOF(
name: envoy.filters.network.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
  stat_prefix: tls_test
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    dry_run: true
  filter_metadata:
    experiment_name: "test_experiment"
    fault_name: "fault0"
)EOF");

  ConnectionStatusCallbacks connect_callbacks;
  auto payload_reader = std::make_shared<WaitForPayloadReader>(*dispatcher_);
  auto ssl_client = makeTlsClientConnection();
  ssl_client->addConnectionCallbacks(connect_callbacks);
  ssl_client->addReadFilter(payload_reader);
  ssl_client->connect();

  while (!connect_callbacks.connected()) {
    ASSERT_FALSE(connect_callbacks.closed());
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Buffer::OwnedImpl write_buf("hello");
  ssl_client->write(write_buf, false);
  payload_reader->setDataToWaitFor("hello");
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  ssl_client->close(Network::ConnectionCloseType::NoFlush);
  while (!connect_callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  test_server_->waitForCounter("fault.tls_test.aborts_dry_run", Eq(1));
  EXPECT_EQ(0U, test_server_->counter("fault.tls_test.aborts_injected")->value());

  const std::string log_entry =
      waitForAccessLog(listener_access_log_name_, /*entry=*/0, /*allow_excess_entries=*/false,
                       /*client_connection=*/ssl_client.get());
  EXPECT_THAT(log_entry, HasSubstr("-|-|-|-|"));
  EXPECT_THAT(log_entry, HasSubstr("\"dry_run_abort\":true"));
  EXPECT_THAT(log_entry, Not(HasSubstr("experiment_name")));
  EXPECT_THAT(log_entry, Not(HasSubstr("fault_name")));
}

} // namespace
} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#include <functional>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/base_overload_integration_test.h"
#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {

using DynamicModuleNetworkFilterProto =
    envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter;
using DynamicModuleConfigModifier = std::function<void(DynamicModuleNetworkFilterProto&)>;

class DynamicModulesNetworkIntegrationTestBase : public BaseIntegrationTest {
public:
  explicit DynamicModulesNetworkIntegrationTestBase(Network::Address::IpVersion version)
      : BaseIntegrationTest(version, ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

protected:
  void initializeFilter(const std::string& filter_name, const std::string& module_name,
                        const std::string& search_path, const std::string& config = "",
                        DynamicModuleConfigModifier modify_config = nullptr) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(search_path), 1);

    DynamicModuleNetworkFilterProto dm_config;
    dm_config.mutable_dynamic_module_config()->set_name(module_name);
    dm_config.set_filter_name(filter_name);
    if (!config.empty()) {
      Protobuf::StringValue filter_config;
      filter_config.set_value(config);
      std::ignore = dm_config.mutable_filter_config()->PackFrom(filter_config);
    }
    if (modify_config != nullptr) {
      modify_config(dm_config);
    }

    config_helper_.addConfigModifier(
        [dm_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);

          // Get the existing tcp_proxy filter.
          auto tcp_proxy_filter = filter_chain->filters(0);

          // Clear and rebuild with dynamic modules first, then tcp_proxy.
          filter_chain->clear_filters();

          // Add the dynamic module filter.
          addDynamicModuleFilter(*filter_chain, dm_config);

          // Add the tcp_proxy back.
          filter_chain->add_filters()->CopyFrom(tcp_proxy_filter);
        });

    BaseIntegrationTest::initialize();
  }

  static void addDynamicModuleFilter(envoy::config::listener::v3::FilterChain& filter_chain,
                                     const DynamicModuleNetworkFilterProto& dm_config) {
    auto* dm_filter = filter_chain.add_filters();
    dm_filter->set_name("envoy.filters.network.dynamic_modules");
    std::ignore = dm_filter->mutable_typed_config()->PackFrom(dm_config);
  }

  static void addFileAccessLog(DynamicModuleNetworkFilterProto& dm_config, const std::string& path,
                               const std::string& format) {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path(path);
    file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        absl::StrCat(format, "\n"));
    auto* access_log = dm_config.add_access_log();
    access_log->set_name("envoy.access_loggers.file");
    std::ignore = access_log->mutable_typed_config()->PackFrom(file_access_log);
  }
};

class DynamicModulesNetworkIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public DynamicModulesNetworkIntegrationTestBase {
public:
  DynamicModulesNetworkIntegrationTest() : DynamicModulesNetworkIntegrationTestBase(GetParam()) {}

  void initializeCFilter(const std::string& filter_name, const std::string& config = "",
                         DynamicModuleConfigModifier modify_config = nullptr) {
    initializeFilter(filter_name, "network_no_op",
                     "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c", config,
                     std::move(modify_config));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesNetworkIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesNetworkIntegrationTest, PassThrough) {
  initializeCFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client to upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send data from upstream to client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  // Half-close to properly close the connection.
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkIntegrationTest, LargeData) {
  initializeCFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send large data from client to upstream with half-close.
  std::string large_data(100000, 'x');
  ASSERT_TRUE(tcp_client->write(large_data, true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(100000));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Close from upstream.
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkIntegrationTest, HalfClose) {
  initializeCFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data and half-close from client.
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Send data and close from upstream.
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// The filter logs the bytes that the TCP proxy behind it counts.
TEST_P(DynamicModulesNetworkIntegrationTest, AccessLogBeforeTcpProxy) {
  const std::string access_log_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  initializeCFilter("passthrough", "",
                    [access_log_path](DynamicModuleNetworkFilterProto& dm_config) {
                      addFileAccessLog(dm_config, access_log_path,
                                       "%ACCESS_LOG_TYPE% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
                                       "%DOWNSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_SENT% "
                                       "%UPSTREAM_WIRE_BYTES_RECEIVED%");
                    });

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("world!", true));
  ASSERT_TRUE(tcp_client->waitForData(6));
  EXPECT_EQ("world!", tcp_client->data());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  EXPECT_EQ("TcpConnectionEnd 5 6 5 6", waitForAccessLog(access_log_path));
}

// The filter logs the connection even when an earlier filter stops the chain before it.
TEST_P(DynamicModulesNetworkIntegrationTest, AccessLogWhenEarlierFilterStopsIteration) {
  const std::string access_log_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);
  config_helper_.addConfigModifier(
      [access_log_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        filter_chain->clear_filters();
        DynamicModuleNetworkFilterProto stopping_config;
        stopping_config.mutable_dynamic_module_config()->set_name("network_stop_iteration");
        addDynamicModuleFilter(*filter_chain, stopping_config);
        DynamicModuleNetworkFilterProto logging_config;
        logging_config.mutable_dynamic_module_config()->set_name("network_no_op");
        logging_config.set_terminal_filter(true);
        addFileAccessLog(logging_config, access_log_path, "%ACCESS_LOG_TYPE% %STREAM_ID%");
        addDynamicModuleFilter(*filter_chain, logging_config);
      });
  BaseIntegrationTest::initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  // The filter never saw the new connection, so it gave the connection no stream ID.
  EXPECT_EQ("TcpConnectionEnd -", waitForAccessLog(access_log_path));
}

// The filter flushes periodic logs while the connection is open, and the log of its end comes last.
TEST_P(DynamicModulesNetworkIntegrationTest, PeriodicAccessLog) {
  const std::string access_log_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  initializeCFilter(
      "passthrough", "", [access_log_path](DynamicModuleNetworkFilterProto& dm_config) {
        addFileAccessLog(dm_config, access_log_path, "%ACCESS_LOG_TYPE%");
        dm_config.mutable_access_log_options()->mutable_access_log_flush_interval()->set_nanos(
            100000000);
      });

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_EQ("TcpPeriodic", waitForAccessLog(access_log_path, 0, /*allow_excess_entries=*/true));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
  test_server_.reset();

  const std::vector<std::string> entries = waitForAccessLogEntries(access_log_path, nullptr, 2);
  EXPECT_EQ("TcpConnectionEnd", entries.back());
  EXPECT_THAT(std::vector<std::string>(entries.begin(), entries.end() - 1),
              testing::Each("TcpPeriodic"));
}

class DynamicModulesNetworkSdkIntegrationTest : public testing::TestWithParam<std::string>,
                                                public DynamicModulesNetworkIntegrationTestBase {
public:
  DynamicModulesNetworkSdkIntegrationTest()
      : DynamicModulesNetworkIntegrationTestBase(GetParam() == "rust"
                                                     ? Envoy::Network::Address::IpVersion::v4
                                                     : Envoy::Network::Address::IpVersion::v6) {}

  void initializeSdkFilter(const std::string& filter_name, const std::string& config = "") {
    initializeFilter(filter_name, "network_integration_test",
                     "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" + GetParam(),
                     config);
  }
};

#ifndef __SANITIZE_ADDRESS__
auto DynamicModulesNetworkSdkIntegrationTestValues = testing::Values("rust", "go", "cpp");
#else
auto DynamicModulesNetworkSdkIntegrationTestValues = testing::Values("rust", "go");
#endif

INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModulesNetworkSdkIntegrationTest,
                         DynamicModulesNetworkSdkIntegrationTestValues,
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(DynamicModulesNetworkSdkIntegrationTest, FlowControl) {
  initializeSdkFilter("flow_control");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client to upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send data from upstream to client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  // Half-close to properly close the connection.
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, ConnectionState) {
  initializeSdkFilter("connection_state");

  // The config factory emits a counter directly from the config context, exercising config-scoped
  // metric emission. Only the Rust and Go SDKs expose config-handle metrics; the C++ SDK does not.
  if (GetParam() != "cpp") {
    test_server_->waitForCounter("dynamicmodulescustom.config_total", testing::Ge(1));
  }

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client to upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send data from upstream to client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  // Half-close to properly close the connection.
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, HalfCloseControl) {
  initializeSdkFilter("half_close");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data and half-close from client.
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Send data and close from upstream.
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, BufferLimits) {
  initializeSdkFilter("buffer_limits");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client to upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send data from upstream to client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  // Half-close to properly close the connection.
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, DynamicMetadata) {
  initializeSdkFilter("dynamic_metadata");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The filter sets dynamic metadata and reads the untyped values back on new connection, checking
  // them in the module. A failure there stops the chain and the upstream never sees data.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, UpstreamConnectionId) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the upstream_connection_id filter is only in the rust test module";
  }

  initializeSdkFilter("upstream_connection_id");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkSdkIntegrationTest, ReadAttributes) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the read_attributes filter is only in the rust test module";
  }

  initializeSdkFilter("read_attributes");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The filter reads source address and connection id on new connection and asserts in the module.
  // A failure there stops the chain and the upstream never sees data.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// A terminal module reports the upstream that it reached itself, and the access logs show the
// report as they do for the TCP proxy.
TEST_P(DynamicModulesNetworkSdkIntegrationTest, AccessLogReadsReportedStreamInfo) {
  if (GetParam() != "rust") {
    GTEST_SKIP() << "the access_log_reporter filter is only in the rust test module";
  }

  const std::string types_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  const std::string report_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);
  config_helper_.addConfigModifier(
      [types_path, report_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        filter_chain->clear_filters();
        DynamicModuleNetworkFilterProto dm_config;
        dm_config.mutable_dynamic_module_config()->set_name("network_integration_test");
        dm_config.set_filter_name("access_log_reporter");
        dm_config.set_terminal_filter(true);
        dm_config.mutable_access_log_options()->set_flush_access_log_on_start(true);
        addFileAccessLog(dm_config, types_path, "%ACCESS_LOG_TYPE%");
        addFileAccessLog(
            dm_config, report_path,
            "%UPSTREAM_HOST% %UPSTREAM_REMOTE_ADDRESS% %UPSTREAM_LOCAL_ADDRESS% %UPSTREAM_CLUSTER% "
            "%UPSTREAM_HOSTS_ATTEMPTED% %UPSTREAM_REQUEST_ATTEMPT_COUNT% "
            "%UPSTREAM_TRANSPORT_FAILURE_REASON% %UPSTREAM_LOCAL_CLOSE_REASON% "
            "%UPSTREAM_DETECTED_CLOSE_TYPE% %RESPONSE_FLAGS% %CUSTOM_FLAGS% "
            "%DOWNSTREAM_WIRE_BYTES_RECEIVED% %DOWNSTREAM_WIRE_BYTES_SENT% "
            "%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
            "%COMMON_DURATION(US_CX_BEG:US_RX_END:us)% %COMMON_DURATION(DS_CX_BEG:DS_CX_END:us)% "
            "%STREAM_ID%");
        addDynamicModuleFilter(*filter_chain, dm_config);
      });
  BaseIntegrationTest::initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->waitForData(10));
  EXPECT_EQ("hellohello", tcp_client->data());
  tcp_client->close();

  EXPECT_EQ("TcpConnectionStart", waitForAccessLog(types_path, 0, /*allow_excess_entries=*/true));
  EXPECT_EQ("TcpPeriodic", waitForAccessLog(types_path, 1, /*allow_excess_entries=*/true));
  EXPECT_EQ("TcpUpstreamConnected", waitForAccessLog(types_path, 2, /*allow_excess_entries=*/true));
  EXPECT_EQ("TcpConnectionEnd", waitForAccessLog(types_path, 3));
  // The periodic log comes before the module reports a host.
  EXPECT_THAT(waitForAccessLog(report_path, 1, /*allow_excess_entries=*/true),
              testing::MatchesRegex("- - - cluster_0 .*"));
  EXPECT_THAT(waitForAccessLog(report_path, 3),
              testing::MatchesRegex(
                  "10\\.1\\.2\\.3:5432 10\\.1\\.2\\.3:5432 10\\.1\\.2\\.100:41000 cluster_0 "
                  "10\\.1\\.2\\.2:5432,10\\.1\\.2\\.3:5432 2 connection_refused downstream_closed "
                  "LocalReset DC reporter 5 10 5 10 [0-9]+ [0-9]+ [0-9a-f-]{36}"));
}

class DynamicModulesNetworkLoadShedPointIntegrationTest
    : public BaseOverloadIntegrationTest,
      public DynamicModulesNetworkSdkIntegrationTest {
public:
  // Saturates the trigger of the load shed point that the module checks.
  void initializeOverloadedLoadShedPoint(bool bypass_overload_manager) {
    setupOverloadManagerConfig(
        TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "test.dynamic_module_load_shed_point"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));
    config_helper_.addConfigModifier(
        [this, bypass_overload_manager](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          *bootstrap.mutable_overload_manager() = overload_manager_config_;
          bootstrap.mutable_static_resources()->mutable_listeners(0)->set_bypass_overload_manager(
              bypass_overload_manager);
        });
    initializeSdkFilter("load_shed_point", "test.dynamic_module_load_shed_point");
    updateResource(0.95);
    test_server_->waitForGauge("overload.test.dynamic_module_load_shed_point.scale_percent",
                               testing::Eq(100));
  }

  void expectProxiedConnection() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    ASSERT_TRUE(tcp_client->write("hello", /*end_stream=*/true));
    ASSERT_TRUE(fake_upstream_connection->waitForData(5));
    ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
    ASSERT_TRUE(fake_upstream_connection->close());
    tcp_client->waitForHalfClose();
    tcp_client->close();
  }
};

INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModulesNetworkLoadShedPointIntegrationTest,
                         DynamicModulesNetworkSdkIntegrationTestValues,
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(DynamicModulesNetworkLoadShedPointIntegrationTest, ShedsLoad) {
  initializeOverloadedLoadShedPoint(/*bypass_overload_manager=*/false);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  // The client enables half close, so it sees the closed connection as the end of the stream.
  tcp_client->waitForHalfClose();
  tcp_client->close();
  test_server_->waitForCounter("overload.test.dynamic_module_load_shed_point.shed_load_count",
                               testing::Eq(1));

  updateResource(0.8);
  test_server_->waitForGauge("overload.test.dynamic_module_load_shed_point.scale_percent",
                             testing::Eq(0));
  expectProxiedConnection();
}

TEST_P(DynamicModulesNetworkLoadShedPointIntegrationTest,
       ListenerBypassingOverloadManagerDoesNotShedLoad) {
  initializeOverloadedLoadShedPoint(/*bypass_overload_manager=*/true);

  expectProxiedConnection();
  EXPECT_EQ(0, test_server_->counter("overload.test.dynamic_module_load_shed_point.shed_load_count")
                   ->value());
}

} // namespace Envoy

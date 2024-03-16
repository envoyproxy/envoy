#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "msgpack.hpp"

using testing::AssertionResult;

namespace Envoy {
namespace {

constexpr char default_cluster_name[] = "fluentd_cluster";
constexpr char default_tag[] = "fluentd_cluster";
constexpr char default_stat_prefix[] = "fluentd_1";

class FluentdAccessLogIntegrationTest : public testing::Test, public BaseIntegrationTest {
public:
  FluentdAccessLogIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

  void init(const std::string cluster_name = default_cluster_name,
            bool flush_access_log_on_connected = false,
            absl::optional<uint32_t> buffer_size_bytes = absl::nullopt) {
    setUpstreamCount(2);
    config_helper_.renameListener("tcp_proxy");
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* access_log_cluster = bootstrap.mutable_static_resources()->add_clusters();
          access_log_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          access_log_cluster->set_name(default_cluster_name);

          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);
          auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

          ASSERT_TRUE(
              config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
          auto tcp_proxy_config =
              MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
                  *config_blob);

          tcp_proxy_config.mutable_access_log_options()->set_flush_access_log_on_connected(
              flush_access_log_on_connected);
          auto* access_log = tcp_proxy_config.add_access_log();
          access_log->set_name("access_log.fluentd");
          envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig access_log_config;
          access_log_config.set_cluster(cluster_name);
          access_log_config.set_tag(default_tag);
          access_log_config.set_stat_prefix(default_stat_prefix);

          if (buffer_size_bytes.has_value()) {
            access_log_config.mutable_buffer_size_bytes()->set_value(buffer_size_bytes.value());
          }

          auto* record = access_log_config.mutable_record();
          (*record->mutable_fields())["Message"].set_string_value("SomeValue");
          (*record->mutable_fields())["LogType"].set_string_value("%ACCESS_LOG_TYPE%");

          access_log->mutable_typed_config()->PackFrom(access_log_config);
          config_blob->PackFrom(tcp_proxy_config);
        });

    BaseIntegrationTest::initialize();
  }

  // The Fluentd records are msgpack serialized, but for testing convenience, we expect
  // the records as JSON strings and later converting while comparing the values.
  void validateFluentdPayload(const std::string& tcp_data, bool* validated,
                              std::vector<std::vector<std::string>> expected_entries) {
    msgpack::unpacker unpacker;
    unpacker.reserve_buffer(tcp_data.size());
    std::memcpy(unpacker.buffer(), tcp_data.data(), tcp_data.size());
    unpacker.buffer_consumed(tcp_data.size());

    size_t entry_index = 0;
    msgpack::object_handle handle;
    while (unpacker.next(handle)) {
      auto& expected_records_as_json = expected_entries[entry_index++];

      msgpack::object message = handle.get();
      ASSERT_EQ(msgpack::type::object_type::ARRAY, message.type);
      ASSERT_EQ(msgpack::type::STR, message.via.array.ptr[0].type);
      ASSERT_EQ(default_tag, message.via.array.ptr[0].as<std::string>());
      ASSERT_EQ(msgpack::type::object_type::ARRAY, message.via.array.ptr[1].type);

      ASSERT_EQ(expected_records_as_json.size(), message.via.array.ptr[1].via.array.size);
      for (size_t idx = 0; idx < expected_records_as_json.size(); idx++) {
        auto& record = message.via.array.ptr[1].via.array.ptr[idx];
        ASSERT_EQ(msgpack::type::object_type::ARRAY, record.type);
        ASSERT_EQ(msgpack::type::object_type::POSITIVE_INTEGER, record.via.array.ptr[0].type);
        ASSERT_GT(record.via.array.ptr[0].as<uint64_t>(), 0);
        ASSERT_EQ(msgpack::type::object_type::MAP, record.via.array.ptr[1].type);

        std::stringstream stream; // msgpack will stream map type of fields as JSON string
        stream << record.via.array.ptr[1];
        std::string record_as_json = stream.str();

        ASSERT_TRUE(TestUtility::jsonStringEqual(expected_records_as_json[idx], record_as_json))
            << fmt::format("expected: {}, actual: {}", expected_records_as_json[idx],
                           record_as_json);
      }
    }

    if (expected_entries.size() == entry_index) {
      *validated = true;
    }
  }

  void sendBidirectionalData() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
    ASSERT_TRUE(tcp_client->write("hello", true));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_connection_));
    ASSERT_TRUE(fake_tcp_connection_->waitForData(5));
    ASSERT_TRUE(fake_tcp_connection_->write("world", true));
    tcp_client->waitForData("world");
    ASSERT_TRUE(fake_tcp_connection_->waitForDisconnect());
    tcp_client->waitForDisconnect();
  }

  FakeRawConnectionPtr fake_tcp_connection_;
  FakeRawConnectionPtr fake_access_log_connection_;
};

TEST_F(FluentdAccessLogIntegrationTest, UnknownCluster) {
  EXPECT_DEATH(init("unknown_cluster"), "");
}

TEST_F(FluentdAccessLogIntegrationTest, SingleEntrySingleRecord) {
  init();
  sendBidirectionalData();

  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.entries_buffered", 1);
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.events_sent", 1);

  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_access_log_connection_));
  EXPECT_TRUE(fake_access_log_connection_->waitForData([&](const std::string& tcp_data) -> bool {
    bool validated = false;
    validateFluentdPayload(tcp_data, &validated,
                           {{"{\"Message\":\"SomeValue\",\"LogType\":\"TcpConnectionEnd\"}"}});
    return validated;
  }));
}

TEST_F(FluentdAccessLogIntegrationTest, SingleEntryTwoRecords) {
  init(default_cluster_name, /*flush_access_log_on_connected = */ true);
  sendBidirectionalData();

  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.entries_buffered", 2);
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.events_sent", 1);

  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_access_log_connection_));
  EXPECT_TRUE(fake_access_log_connection_->waitForData([&](const std::string& tcp_data) -> bool {
    bool validated = false;
    validateFluentdPayload(tcp_data, &validated,
                           {{"{\"Message\":\"SomeValue\",\"LogType\":\"TcpUpstreamConnected\"}",
                             "{\"Message\":\"SomeValue\",\"LogType\":\"TcpConnectionEnd\"}"}});
    return validated;
  }));
}

TEST_F(FluentdAccessLogIntegrationTest, TwoEntries) {
  init(default_cluster_name, /*flush_access_log_on_connected = */ true, /*buffer_size_bytes = */ 0);
  sendBidirectionalData();

  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.entries_buffered", 2);
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.events_sent", 2);

  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_access_log_connection_));
  EXPECT_TRUE(fake_access_log_connection_->waitForData([&](const std::string& tcp_data) -> bool {
    bool validated = false;
    validateFluentdPayload(tcp_data, &validated,
                           {{"{\"Message\":\"SomeValue\",\"LogType\":\"TcpUpstreamConnected\"}"},
                            {"{\"Message\":\"SomeValue\",\"LogType\":\"TcpConnectionEnd\"}"}});
    return validated;
  }));
}

TEST_F(FluentdAccessLogIntegrationTest, UpstreamConnectionClosed) {
  init();
  sendBidirectionalData();

  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.entries_buffered", 1);
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.events_sent", 1);

  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_access_log_connection_));
  EXPECT_TRUE(fake_access_log_connection_->waitForData([&](const std::string& tcp_data) -> bool {
    bool validated = false;
    validateFluentdPayload(tcp_data, &validated,
                           {{"{\"Message\":\"SomeValue\",\"LogType\":\"TcpConnectionEnd\"}"}});
    return validated;
  }));

  ASSERT_TRUE(fake_access_log_connection_->close());
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.connections_closed", 1);

  // New access log would be discarded because the connection is closed.
  sendBidirectionalData();
  test_server_->waitForCounterEq("access_logs.fluentd.fluentd_1.entries_lost", 1);
}

} // namespace
} // namespace Envoy

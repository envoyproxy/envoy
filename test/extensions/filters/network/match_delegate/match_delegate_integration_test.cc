#include "source/common/network/socket_option_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/network/match_delegate/config.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {
namespace {

using envoy::extensions::common::matching::v3::ExtensionWithMatcher;
using Envoy::Protobuf::StringValue;
using Envoy::Protobuf::UInt32Value;

// A simple network filter that counts connections and data.
class CountingFilter : public Network::Filter {
public:
  // Read filter methods
  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    absl::MutexLock lock(mutex_);
    data_bytes_ += data.length();
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    absl::MutexLock lock(mutex_);
    connection_count_++;
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Write filter methods
  Network::FilterStatus onWrite(Buffer::Instance& data, bool) override {
    absl::MutexLock lock(mutex_);
    write_bytes_ += data.length();
    return Network::FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  // Thread-safe getters for counter values
  static uint32_t getConnectionCount() {
    absl::MutexLock lock(mutex_);
    return connection_count_;
  }

  static uint64_t getDataBytes() {
    absl::MutexLock lock(mutex_);
    return data_bytes_;
  }

  static uint64_t getWriteBytes() {
    absl::MutexLock lock(mutex_);
    return write_bytes_;
  }

  // Reset all counters
  static void resetCounters() {
    absl::MutexLock lock(mutex_);
    connection_count_ = 0;
    data_bytes_ = 0;
    write_bytes_ = 0;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  // Static counters and mutex for thread-safety
  static absl::Mutex mutex_;
  static uint32_t connection_count_ ABSL_GUARDED_BY(mutex_);
  static uint64_t data_bytes_ ABSL_GUARDED_BY(mutex_);
  static uint64_t write_bytes_ ABSL_GUARDED_BY(mutex_);
};

// Initialize static members
absl::Mutex CountingFilter::mutex_;
uint32_t CountingFilter::connection_count_ = 0;
uint64_t CountingFilter::data_bytes_ = 0;
uint64_t CountingFilter::write_bytes_ = 0;

// Filter that sets filter state for testing
class SetFilterStateFilter : public Network::ReadFilter {
public:
  explicit SetFilterStateFilter(const std::string& value) : value_(value) {}

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    // Set the filter state value when a new connection is established
    if (!value_.empty()) {
      read_callbacks_->connection().streamInfo().filterState()->setData(
          "test_key", std::make_shared<Router::StringAccessorImpl>(value_),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    }
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  const std::string value_;
};

// Config for our counting filter
class CountingFilterConfig : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.counting_filter"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<UInt32Value>(); // Using UInt32Value here
  }

  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](auto& filter_manager) {
      auto filter = std::make_shared<CountingFilter>();
      filter_manager.addFilter(filter);
    };
  }
};

// Config for our filter state setting filter
class SetFilterStateFilterConfig : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.set_filter_state"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StringValue>();
  }

  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext&) override {
    const auto& config = dynamic_cast<const StringValue&>(proto_config);
    return [value = config.value()](auto& filter_manager) {
      auto filter = std::make_shared<SetFilterStateFilter>(value);
      filter_manager.addReadFilter(filter);
    };
  }
};

class MatchDelegateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  MatchDelegateIntegrationTest() : BaseIntegrationTest(GetParam(), configureBaseConfig()) {
    enableHalfClose(true);
  }

  static std::string configureBaseConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        # First filter sets filter state
        - name: envoy.test.set_filter_state
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: ""  # default empty value, will be overridden in specific tests
        # Then the match delegate filter wrapping our counting filter
        - name: match_delegate_filter
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
            extension_config:
              name: envoy.test.counting_filter
              typed_config:
                "@type": type.googleapis.com/google.protobuf.UInt32Value
            xds_matcher:
              matcher_tree:
                input:
                  name: filter-state
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
                    key: test_key
                exact_match_map:
                  map:
                    "skip":
                      action:
                        name: skip
                        typed_config:
                          "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcp_stats
            cluster: cluster_0
)EOF");
  }

  void initialize() override {
    // Reset the counting filter's counters before each test
    CountingFilter::resetCounters();
    BaseIntegrationTest::initialize();
  }

  // Helper to create config with specific filter state value
  void initializeWithFilterStateValue(const std::string& value) {
    config_helper_.addConfigModifier([value](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filter = filter_chain->mutable_filters(0);

      auto* typed_config = filter->mutable_typed_config();
      StringValue string_value;
      string_value.set_value(value);
      typed_config->PackFrom(string_value);
    });

    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Register our test filters
REGISTER_FACTORY(CountingFilterConfig, Server::Configuration::NamedNetworkFilterConfigFactory);
REGISTER_FACTORY(SetFilterStateFilterConfig,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

// Test with empty filter state (should NOT skip the counting filter)
TEST_P(MatchDelegateIntegrationTest, NoFilterStateValueTest) {
  initializeWithFilterStateValue(""); // Empty value won't cause a skip

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send some test data
  std::string test_data = "hello world";
  ASSERT_TRUE(tcp_client->write(test_data));
  ASSERT_TRUE(fake_upstream_connection->waitForData(test_data.size()));

  // Send response back
  ASSERT_TRUE(fake_upstream_connection->write("response data"));
  tcp_client->waitForData("response data");

  // Verify the counting filter recorded this connection and data
  EXPECT_EQ(1u, CountingFilter::getConnectionCount());
  EXPECT_EQ(test_data.size(), CountingFilter::getDataBytes());
  EXPECT_EQ(13u, CountingFilter::getWriteBytes()); // "response data"

  // Clean up connections
  tcp_client->close();
}

// Test with "skip" filter state (should SKIP the counting filter)
TEST_P(MatchDelegateIntegrationTest, SkipFilterStateValueTest) {
  initializeWithFilterStateValue("skip"); // This value will trigger the skip action

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send some test data
  std::string test_data = "hello world";
  ASSERT_TRUE(tcp_client->write(test_data));
  ASSERT_TRUE(fake_upstream_connection->waitForData(test_data.size()));

  // Send response back
  ASSERT_TRUE(fake_upstream_connection->write("response data"));
  tcp_client->waitForData("response data");

  // The counting filter should not have recorded anything (skipped)
  EXPECT_EQ(0u, CountingFilter::getConnectionCount());
  EXPECT_EQ(0u, CountingFilter::getDataBytes());
  EXPECT_EQ(0u, CountingFilter::getWriteBytes());

  // Clean up connections
  tcp_client->close();
}

// Test with non-matching filter state (should NOT skip the counting filter)
TEST_P(MatchDelegateIntegrationTest, NonMatchingFilterStateValueTest) {
  initializeWithFilterStateValue("other_value"); // This value doesn't match the "skip" action

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send some test data
  std::string test_data = "hello world";
  ASSERT_TRUE(tcp_client->write(test_data));
  ASSERT_TRUE(fake_upstream_connection->waitForData(test_data.size()));

  // Send response back
  ASSERT_TRUE(fake_upstream_connection->write("response data"));
  tcp_client->waitForData("response data");

  // Verify the counting filter recorded this connection and data
  EXPECT_EQ(1u, CountingFilter::getConnectionCount());
  EXPECT_EQ(test_data.size(), CountingFilter::getDataBytes());
  EXPECT_EQ(13u, CountingFilter::getWriteBytes()); // "response data"

  // Clean up connections
  tcp_client->close();
}

} // namespace
} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

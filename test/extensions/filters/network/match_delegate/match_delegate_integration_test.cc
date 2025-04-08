#include "source/common/network/socket_option_impl.h"
#include "source/extensions/filters/network/match_delegate/config.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {
namespace {

using envoy::extensions::common::matching::v3::ExtensionWithMatcher;
using Envoy::ProtobufWkt::StringValue;

// A simple network filter that counts connections and data
// Thread-safe implementation with mutex protection
class CountingFilter : public Network::Filter {
public:
  // Read filter methods
  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    absl::MutexLock lock(&mutex_);
    data_bytes_ += data.length();
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    absl::MutexLock lock(&mutex_);
    connection_count_++;
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Write filter methods
  Network::FilterStatus onWrite(Buffer::Instance& data, bool) override {
    absl::MutexLock lock(&mutex_);
    write_bytes_ += data.length();
    return Network::FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  // Thread-safe getters for counter values
  static uint32_t getConnectionCount() {
    absl::MutexLock lock(&mutex_);
    return connection_count_;
  }

  static uint64_t getDataBytes() {
    absl::MutexLock lock(&mutex_);
    return data_bytes_;
  }

  static uint64_t getWriteBytes() {
    absl::MutexLock lock(&mutex_);
    return write_bytes_;
  }

  // Reset all counters
  static void resetCounters() {
    absl::MutexLock lock(&mutex_);
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

// Config for our test filter
class CountingFilterConfig : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.counting_filter"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StringValue>();
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

class MatchDelegateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  MatchDelegateIntegrationTest()
      : BaseIntegrationTest(GetParam(), configureNetworkDelegateFilter()) {
    enableHalfClose(true);
  }

  static std::string configureNetworkDelegateFilter() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        # First the match delegate filter wrapping our counting filter
        - name: match_delegate_filter
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
            extension_config:
              name: envoy.test.counting_filter
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
            xds_matcher:
              matcher_tree:
                input:
                  name: source-port
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourcePortInput
                exact_match_map:
                  map:
                    "1234":
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Register our test filter
REGISTER_FACTORY(CountingFilterConfig, Server::Configuration::NamedNetworkFilterConfigFactory);

// Test with normal port (should NOT skip the counting filter)
TEST_P(MatchDelegateIntegrationTest, NormalPortTest) {
  initialize();

  // Create a client connection with a normal source port (not 1234)
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

// Test with source port 1234 (should SKIP the counting filter)
TEST_P(MatchDelegateIntegrationTest, MatchingPortSkipTest) {
  initialize();

  // Create a client connection with source port 1234
  std::shared_ptr<Network::Address::Instance> source_address;
  if (version_ == Network::Address::IpVersion::v4) {
    source_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
  } else {
    source_address = std::make_shared<Network::Address::Ipv6Instance>("::1", 1234);
  }
  IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("listener_0"), nullptr, source_address);

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

} // namespace
} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

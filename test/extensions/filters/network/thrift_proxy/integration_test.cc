#include "test/extensions/filters/network/thrift_proxy/integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::Combine;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {
std::string thrift_config;
} // namespace

class ThriftConnManagerIntegrationTest
    : public BaseThriftIntegrationTest,
      public TestWithParam<std::tuple<TransportType, ProtocolType, bool>> {
public:
  ThriftConnManagerIntegrationTest() : BaseThriftIntegrationTest(thrift_config) {}

  static void SetUpTestCase() {
    thrift_config = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.thrift_proxy
          config:
            stat_prefix: thrift_stats
            route_config:
              name: "routes"
              routes:
                - match: {}
                  route:
                    cluster: "cluster_0"
      )EOF";
  }

  void initializeCall(DriverMode mode) {
    TransportType transport;
    ProtocolType protocol;
    bool multiplexed;
    std::tie(transport, protocol, multiplexed) = GetParam();

    absl::optional<std::string> service_name;
    if (multiplexed) {
      service_name = "svcname";
    }

    PayloadOptions options(transport, protocol, mode, service_name, "execute");
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() > 0);
    initializeCommon();
  }

  void initializeOneway() {
    TransportType transport;
    ProtocolType protocol;
    bool multiplexed;
    std::tie(transport, protocol, multiplexed) = GetParam();

    absl::optional<std::string> service_name;
    if (multiplexed) {
      service_name = "svcname";
    }

    PayloadOptions options(transport, protocol, DriverMode::Success, service_name, "poke");
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() == 0);
    initializeCommon();
  }

  void initializeCommon() { BaseThriftIntegrationTest::initialize(); }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  Buffer::OwnedImpl request_bytes_;
  Buffer::OwnedImpl response_bytes_;
};

static std::string
paramToString(const TestParamInfo<std::tuple<TransportType, ProtocolType, bool>>& params) {
  TransportType transport;
  ProtocolType protocol;
  bool multiplexed;
  std::tie(transport, protocol, multiplexed) = params.param;

  std::string transport_name = TransportNames::get().fromType(transport);
  transport_name[0] = absl::ascii_toupper(transport_name[0]);

  std::string protocol_name = ProtocolNames::get().fromType(protocol);
  protocol_name[0] = absl::ascii_toupper(protocol_name[0]);

  if (multiplexed) {
    return fmt::format("{}{}Multiplexed", transport_name, protocol_name);
  }
  return fmt::format("{}{}", transport_name, protocol_name);
}

INSTANTIATE_TEST_CASE_P(TransportAndProtocol, ThriftConnManagerIntegrationTest,
                        Combine(Values(TransportType::Framed, TransportType::Unframed),
                                Values(ProtocolType::Binary, ProtocolType::Compact),
                                Values(false, true)),
                        paramToString);

TEST_P(ThriftConnManagerIntegrationTest, Success) {
  initializeCall(DriverMode::Success);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  ASSERT_TRUE(fake_upstream_connection->write(response_bytes_.toString()));

  tcp_client->waitForData(response_bytes_.toString());
  tcp_client->close();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_success");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, IDLException) {
  initializeCall(DriverMode::IDLException);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  ASSERT_TRUE(fake_upstream_connection->write(response_bytes_.toString()));

  tcp_client->waitForData(response_bytes_.toString());
  tcp_client->close();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_error");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, Exception) {
  initializeCall(DriverMode::Exception);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  ASSERT_TRUE(fake_upstream_connection->write(response_bytes_.toString()));

  tcp_client->waitForData(response_bytes_.toString());
  tcp_client->close();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_exception");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, Oneway) {
  initializeOneway();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  tcp_client->close();

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_oneway");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, OnewayEarlyClose) {
  initializeOneway();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());
  tcp_client->close();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_oneway");
  EXPECT_EQ(1U, counter->value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

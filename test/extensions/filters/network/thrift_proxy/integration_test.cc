#include "test/extensions/filters/network/thrift_proxy/integration.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
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

class ThriftConnManagerIntegrationTest
    : public BaseThriftIntegrationTest,
      public TestWithParam<std::tuple<TransportType, ProtocolType, bool>> {
public:
  static void SetUpTestCase() {
    thrift_config_ = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.thrift_proxy
          config:
            stat_prefix: thrift_stats
            route_config:
              name: "routes"
              routes:
                - match:
                    service_name: "svcname"
                  route:
                    cluster: "cluster_0"
                - match:
                    method_name: "execute"
                    headers:
                    - name: "x-header-1"
                      exact_match: "x-value-1"
                    - name: "x-header-2"
                      regex_match: "0.[5-9]"
                    - name: "x-header-3"
                      range_match:
                        start: 100
                        end: 200
                    - name: "x-header-4"
                      prefix_match: "user_id:"
                    - name: "x-header-5"
                      suffix_match: "asdf"
                  route:
                    cluster: "cluster_1"
                - match:
                    method_name: "execute"
                  route:
                    cluster: "cluster_2"
                - match:
                    method_name: "poke"
                  route:
                    cluster: "cluster_3"
      )EOF";
  }

  void initializeCall(DriverMode mode) {
    std::tie(transport_, protocol_, multiplexed_) = GetParam();

    absl::optional<std::string> service_name;
    if (multiplexed_) {
      service_name = "svcname";
    }

    std::vector<std::pair<std::string, std::string>> headers;
    if (transport_ == TransportType::Header) {
      headers.push_back(std::make_pair("x-header-1", "x-value-1"));
      headers.push_back(std::make_pair("x-header-2", "0.6"));
      headers.push_back(std::make_pair("x-header-3", "150"));
      headers.push_back(std::make_pair("x-header-4", "user_id:10"));
      headers.push_back(std::make_pair("x-header-5", "garbage_asdf"));
    }

    PayloadOptions options(transport_, protocol_, mode, service_name, "execute", {}, headers);
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() > 0);
    initializeCommon();
  }

  void initializeOneway() {
    std::tie(transport_, protocol_, multiplexed_) = GetParam();

    absl::optional<std::string> service_name;
    if (multiplexed_) {
      service_name = "svcname";
    }

    PayloadOptions options(transport_, protocol_, DriverMode::Success, service_name, "poke");
    preparePayloads(options, request_bytes_, response_bytes_);
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() == 0);
    initializeCommon();
  }

  // We allocate as many upstreams as there are clusters, with each upstream being allocated
  // to clusters in the order they're defined in the bootstrap config.
  void initializeCommon() {
    setUpstreamCount(4);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      for (int i = 1; i < 4; i++) {
        auto* c = bootstrap.mutable_static_resources()->add_clusters();
        c->MergeFrom(bootstrap.static_resources().clusters()[0]);
        c->set_name(fmt::format("cluster_{}", i));
      }
    });

    BaseThriftIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

protected:
  // Multiplexed requests are handled by the service name route match,
  // while oneway's are handled by the "poke" method. All other requests
  // are handled by "execute".
  FakeUpstream* getExpectedUpstream(bool oneway) {
    int upstreamIdx = 2;
    if (multiplexed_) {
      upstreamIdx = 0;
    } else if (oneway) {
      upstreamIdx = 3;
    } else if (transport_ == TransportType::Header) {
      upstreamIdx = 1;
    }

    return fake_upstreams_[upstreamIdx].get();
  }

  TransportType transport_;
  ProtocolType protocol_;
  bool multiplexed_;

  std::string result_;

  Buffer::OwnedImpl request_bytes_;
  Buffer::OwnedImpl response_bytes_;
};

static std::string
paramToString(const TestParamInfo<std::tuple<TransportType, ProtocolType, bool>>& params) {
  TransportType transport;
  ProtocolType protocol;
  bool multiplexed;
  std::tie(transport, protocol, multiplexed) = params.param;

  std::string transport_name = transportNameForTest(transport);
  std::string protocol_name = protocolNameForTest(protocol);

  if (multiplexed) {
    return fmt::format("{}{}Multiplexed", transport_name, protocol_name);
  }
  return fmt::format("{}{}", transport_name, protocol_name);
}

INSTANTIATE_TEST_CASE_P(
    TransportAndProtocol, ThriftConnManagerIntegrationTest,
    Combine(Values(TransportType::Framed, TransportType::Unframed, TransportType::Header),
            Values(ProtocolType::Binary, ProtocolType::Compact), Values(false, true)),
    paramToString);

TEST_P(ThriftConnManagerIntegrationTest, Success) {
  initializeCall(DriverMode::Success);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeRawConnectionPtr fake_upstream_connection;
  FakeUpstream* expected_upstream = getExpectedUpstream(false);
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
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

  FakeUpstream* expected_upstream = getExpectedUpstream(false);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
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

  FakeUpstream* expected_upstream = getExpectedUpstream(false);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
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

TEST_P(ThriftConnManagerIntegrationTest, EarlyClose) {
  initializeCall(DriverMode::Success);

  const std::string partial_request =
      request_bytes_.toString().substr(0, request_bytes_.length() - 5);

  FakeUpstream* expected_upstream = getExpectedUpstream(false);
  expected_upstream->set_allow_unexpected_disconnects(true);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(partial_request);
  tcp_client->close();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));

  test_server_->waitForCounterGe("thrift.thrift_stats.cx_destroy_remote_with_active_rq", 1);

  Stats::CounterSharedPtr counter =
      test_server_->counter("thrift.thrift_stats.cx_destroy_remote_with_active_rq");
  EXPECT_EQ(1U, counter->value());
}

// Tests when the downstream client closes before completing a request but an upstream has already
// been connected/assigned.
TEST_P(ThriftConnManagerIntegrationTest, EarlyCloseWithUpstream) {
  initializeCall(DriverMode::Success);

  const std::string partial_request =
      request_bytes_.toString().substr(0, request_bytes_.length() - 5);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(partial_request);

  FakeUpstream* expected_upstream = getExpectedUpstream(false);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));

  tcp_client->close();

  test_server_->waitForCounterGe("thrift.thrift_stats.cx_destroy_remote_with_active_rq", 1);

  Stats::CounterSharedPtr counter =
      test_server_->counter("thrift.thrift_stats.cx_destroy_remote_with_active_rq");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, Oneway) {
  initializeOneway();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(request_bytes_.toString());

  FakeUpstream* expected_upstream = getExpectedUpstream(true);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
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

  FakeUpstream* expected_upstream = getExpectedUpstream(true);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(request_bytes_.toString(), upstream_request.toString());

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_oneway");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftConnManagerIntegrationTest, OnewayEarlyClosePartialRequest) {
  initializeOneway();

  const std::string partial_request =
      request_bytes_.toString().substr(0, request_bytes_.length() - 1);

  FakeUpstream* expected_upstream = getExpectedUpstream(true);
  expected_upstream->set_allow_unexpected_disconnects(true);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(partial_request);
  tcp_client->close();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(expected_upstream->waitForRawConnection(fake_upstream_connection));

  test_server_->waitForCounterGe("thrift.thrift_stats.cx_destroy_remote_with_active_rq", 1);

  Stats::CounterSharedPtr counter =
      test_server_->counter("thrift.thrift_stats.cx_destroy_remote_with_active_rq");
  EXPECT_EQ(1U, counter->value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#include <stdio.h>

#include <fstream>

#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
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

std::string thrift_config;

enum class CallResult {
  Success,
  IDLException,
  Exception,
};

class ThriftFilterIntegrationTest
    : public BaseIntegrationTest,
      public TestWithParam<std::tuple<std::string, std::string, bool>> {
public:
  ThriftFilterIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, thrift_config) {}

  static void SetUpTestCase() {
    thrift_config = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.thrift_proxy
          config:
            stat_prefix: thrift_stats
        - name: envoy.tcp_proxy
          config:
            stat_prefix: tcp_stats
            cluster: cluster_0
      )EOF";
  }

  void initializeCall(CallResult result) {
    std::tie(transport_, protocol_, multiplexed_) = GetParam();

    std::string result_mode;
    switch (result) {
    case CallResult::Success:
      result_mode = "success";
      break;
    case CallResult::IDLException:
      result_mode = "idl-exception";
      break;
    case CallResult::Exception:
      result_mode = "exception";
      break;
    default:
      NOT_REACHED;
    }

    preparePayloads(result_mode, "execute");
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() > 0);

    BaseIntegrationTest::initialize();
  }

  void initializeOneway() {
    std::tie(transport_, protocol_, multiplexed_) = GetParam();

    preparePayloads("success", "poke");
    ASSERT(request_bytes_.length() > 0);
    ASSERT(response_bytes_.length() == 0);

    BaseIntegrationTest::initialize();
  }

  void preparePayloads(std::string result_mode, std::string method) {
    std::vector<std::string> args = {
        TestEnvironment::runfilesPath(
            "test/extensions/filters/network/thrift_proxy/driver/generate_fixture.sh"),
        result_mode,
        transport_,
        protocol_,
    };

    if (multiplexed_) {
      args.push_back("svcname");
    }
    args.push_back("--");
    args.push_back(method);

    TestEnvironment::exec(args);

    std::stringstream file_base;
    file_base << "{{ test_tmpdir }}/" << transport_ << "-" << protocol_ << "-";
    if (multiplexed_) {
      file_base << "svcname-";
    }
    file_base << result_mode;

    readAll(file_base.str() + ".request", request_bytes_);
    readAll(file_base.str() + ".response", response_bytes_);
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

protected:
  void readAll(std::string file, Buffer::OwnedImpl& buffer) {
    file = TestEnvironment::substitute(file, version_);

    std::ifstream is(file, std::ios::binary | std::ios::ate);
    RELEASE_ASSERT(!is.fail());

    std::ifstream::pos_type len = is.tellg();
    if (len > 0) {
      std::vector<char> bytes(len, 0);
      is.seekg(0, std::ios::beg);
      RELEASE_ASSERT(!is.fail());

      is.read(bytes.data(), len);
      RELEASE_ASSERT(!is.fail());

      buffer.add(bytes.data(), len);
    }
  }

  std::string transport_;
  std::string protocol_;
  bool multiplexed_;

  std::string result_;

  Buffer::OwnedImpl request_bytes_;
  Buffer::OwnedImpl response_bytes_;
};

static std::string
paramToString(const TestParamInfo<std::tuple<std::string, std::string, bool>>& params) {
  std::string transport, protocol;
  bool multiplexed;
  std::tie(transport, protocol, multiplexed) = params.param;
  transport = StringUtil::toUpper(absl::string_view(transport).substr(0, 1)) + transport.substr(1);
  protocol = StringUtil::toUpper(absl::string_view(protocol).substr(0, 1)) + protocol.substr(1);
  if (multiplexed) {
    return fmt::format("{}{}Multiplexed", transport, protocol);
  }
  return fmt::format("{}{}", transport, protocol);
}

INSTANTIATE_TEST_CASE_P(
    TransportAndProtocol, ThriftFilterIntegrationTest,
    Combine(Values(TransportNames::get().FRAMED, TransportNames::get().UNFRAMED),
            Values(ProtocolNames::get().BINARY, ProtocolNames::get().COMPACT), Values(false, true)),
    paramToString);

TEST_P(ThriftFilterIntegrationTest, Success) {
  initializeCall(CallResult::Success);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(TestUtility::bufferToString(request_bytes_));

  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  Buffer::OwnedImpl upstream_request(
      fake_upstream_connection->waitForData(request_bytes_.length()));
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));

  fake_upstream_connection->write(TestUtility::bufferToString(response_bytes_));

  tcp_client->waitForData(TestUtility::bufferToString(response_bytes_));
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_success");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftFilterIntegrationTest, IDLException) {
  initializeCall(CallResult::IDLException);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(TestUtility::bufferToString(request_bytes_));

  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  Buffer::OwnedImpl upstream_request(
      fake_upstream_connection->waitForData(request_bytes_.length()));
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));

  fake_upstream_connection->write(TestUtility::bufferToString(response_bytes_));

  tcp_client->waitForData(TestUtility::bufferToString(response_bytes_));
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_error");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftFilterIntegrationTest, Exception) {
  initializeCall(CallResult::Exception);

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(TestUtility::bufferToString(request_bytes_));

  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  Buffer::OwnedImpl upstream_request(
      fake_upstream_connection->waitForData(request_bytes_.length()));
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));

  fake_upstream_connection->write(TestUtility::bufferToString(response_bytes_));

  tcp_client->waitForData(TestUtility::bufferToString(response_bytes_));
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_exception");
  EXPECT_EQ(1U, counter->value());
}

TEST_P(ThriftFilterIntegrationTest, Oneway) {
  initializeOneway();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(TestUtility::bufferToString(request_bytes_));

  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  Buffer::OwnedImpl upstream_request(
      fake_upstream_connection->waitForData(request_bytes_.length()));
  EXPECT_TRUE(TestUtility::buffersEqual(upstream_request, request_bytes_));

  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_oneway");
  EXPECT_EQ(1U, counter->value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

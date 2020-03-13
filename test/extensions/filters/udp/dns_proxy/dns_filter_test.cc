#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.h"
#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.validate.h"

#include "common/common/logger.h"

#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::ByMove;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.rc_ = rc;
  return no_error;
}

/*
class TestDnsFilter : public DnsFilter {
public:
  using DnsFilter::DnsFilter;
};
*/

class DnsFilterTest : public testing::Test {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")) {
    // Logger::Registry::setLogLevel(TestEnvironment::getOptions().logLevel());
    Logger::Registry::setLogLevel(spdlog::level::trace);
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_.udp_listener_, send(_))
        .WillRepeatedly(
            Invoke([this](const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
              response_ptr = std::make_unique<Buffer::OwnedImpl>();
              response_ptr->move(send_data.buffer_);
              return makeNoError(response_ptr->length());
            }));
  }

  ~DnsFilterTest() { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const std::string& yaml) {
    envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    EXPECT_CALL(listener_factory_, scope()).WillOnce(ReturnRef(*store));
    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  void sendQueryFromClient(const std::string& peer_address, const std::string& buffer) {
    Network::UdpRecvData data;
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(peer_address);
    data.addresses_.local_ = listener_address_;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  std::string buildQueryForDomain(const std::string& name, uint16_t rec_type, uint16_t rec_class) {

    DnsMessageStruct query{};

    // Generate a random query ID
    query.id = rng_.random() & 0xFFFF;

    // Signify that this is a query
    query.f.flags.qr = 0;

    // This should usually be zero
    query.f.flags.opcode = 0;

    query.f.flags.aa = 0;
    query.f.flags.tc = 0;

    // Set Recursion flags (at least one bit set so that the flags are not all zero)
    query.f.flags.rd = 1;
    query.f.flags.ra = 0;

    // reserved flag is not set
    query.f.flags.z = 0;

    // Set the authenticated flags to zero
    query.f.flags.ad = 0;
    query.f.flags.cd = 0;

    query.questions = 1;
    query.answers = 0;
    query.authority_rrs = 0;
    query.additional_rrs = 0;

    Buffer::OwnedImpl buffer_;
    buffer_.writeBEInt<uint16_t>(query.id);
    buffer_.writeBEInt<uint16_t>(query.f.val);
    buffer_.writeBEInt<uint16_t>(query.questions);
    buffer_.writeBEInt<uint16_t>(query.answers);
    buffer_.writeBEInt<uint16_t>(query.authority_rrs);
    buffer_.writeBEInt<uint16_t>(query.additional_rrs);

    DnsQueryRecordPtr query_ptr = std::make_unique<DnsQueryRecord>(name, rec_type, rec_class);

    buffer_.add(query_ptr->serialize());

    return buffer_.toString();
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  Server::Configuration::MockListenerFactoryContext listener_factory_;
  DnsFilterEnvoyConfigSharedPtr config_;

  std::unique_ptr<DnsFilter> filter_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Stats::IsolatedStoreImpl stats_store_;
  Buffer::InstancePtr response_ptr;
  DnsResponseParser response_parser_;
  Runtime::RandomGeneratorImpl rng_;

  // TestDnsFilter parent_filter_;

  // NiceMock<MockTimeSystem> time_system_;

  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
client_config:
  forward_query: true
  dns_query_timeout: 3s
  upstream_resolvers:
    - "1.1.1.1"
    - "8.8.8.8"
    - "8.8.4.4"
server_config:
  retry_count: 3
  virtual_domains:
    - name: "www.foo1.com"
      endpoint:
        type: STATIC
        address:
          - 10.0.0.1
          - 10.0.0.2
    - name: "www.foo2.com"
      endpoint:
        type: STATIC
        address:
          - 10.0.2.1
    - name: "www.foo3.com"
      endpoint:
        type: STATIC
        address:
          - 10.0.3.1
  )EOF";
};

TEST_F(DnsFilterTest, InvalidQuery) {
  InSequence s;

  setup(config_yaml);

  sendQueryFromClient("10.0.0.1:1000", "hello");

  bool ret = response_parser_.parseResponseData(response_ptr);
  ASSERT_TRUE(ret);

  ASSERT_EQ(0, response_parser_.getQueries().size());
  ASSERT_EQ(0, response_parser_.getAnswers().size());
  ASSERT_EQ(3, response_parser_.getQueryResponseCode());
}

TEST_F(DnsFilterTest, SingleValidQuery) {
  InSequence s;

  setup(config_yaml);

  uint16_t rec_type = 1;
  uint16_t rec_class = 1;
  const std::string query = buildQueryForDomain("www.foo3.com", rec_type, rec_class);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  bool ret = response_parser_.parseResponseData(response_ptr);
  ASSERT_TRUE(ret);

  ASSERT_EQ(1, response_parser_.getQueries().size());
  ASSERT_EQ(1, response_parser_.getAnswers().size());
  ASSERT_EQ(0, response_parser_.getQueryResponseCode());
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

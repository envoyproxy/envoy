#include "source/common/buffer/buffer_impl.h"

#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/broker/rewriter.h"
#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/test/broker/mock_filter_config.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

static void putBytesIntoBuffer(Buffer::Instance& buffer, const uint32_t size) {
  std::vector<char> data(size, 42);
  absl::string_view sv = {data.data(), data.size()};
  buffer.add(sv);
}

static Buffer::InstancePtr makeRandomBuffer(const uint32_t size) {
  Buffer::InstancePtr result = std::make_unique<Buffer::OwnedImpl>();
  putBytesIntoBuffer(*result, size);
  return result;
}

class FakeResponse : public AbstractResponse {
public:
  FakeResponse(const size_t size) : AbstractResponse{ResponseMetadata{-1, 0, 0}}, size_{size} {}

  uint32_t computeSize() const override { return size_; };

  uint32_t encode(Buffer::Instance& dst) const override {
    putBytesIntoBuffer(dst, size_);
    return size_;
  };

private:
  size_t size_;
};

TEST(ResponseRewriterImplUnitTest, ShouldRewriteBuffer) {
  // given
  ResponseRewriterImpl testee{std::make_shared<MockBrokerFilterConfig>()};

  auto response1 = std::make_shared<FakeResponse>(7);
  auto response2 = std::make_shared<FakeResponse>(13);
  auto response3 = std::make_shared<FakeResponse>(42);

  // when - 1
  testee.onMessage(response1);
  testee.onMessage(response2);
  testee.onMessage(response3);

  // then - 1
  ASSERT_EQ(testee.getStoredResponseCountForTest(), 3);

  // when - 2
  auto buffer = makeRandomBuffer(4242);
  testee.process(*buffer);

  // then - 2
  ASSERT_EQ(testee.getStoredResponseCountForTest(), 0);
  ASSERT_EQ(buffer->length(), (3 * 4) + 7 + 13 + 42); // 4 bytes for message length
}

template <typename T>
static void assertAddress(const T& arg, const std::string& host, const int32_t port) {
  ASSERT_EQ(arg.host_, host);
  ASSERT_EQ(arg.port_, port);
}

TEST(ResponseRewriterImplUnitTest, ShouldRewriteMetadataResponse) {
  // given
  MetadataResponseBroker b1 = {13, "host1", 1111};
  MetadataResponseBroker b2 = {42, "host2", 2222};
  MetadataResponseBroker b3 = {77, "host3", 3333};
  std::vector<MetadataResponseBroker> brokers = {b1, b2, b3};
  MetadataResponse mr = {brokers, {}};

  auto config = std::make_shared<MockBrokerFilterConfig>();
  absl::optional<HostAndPort> r1 = {{"nh1", 4444}};
  EXPECT_CALL(*config, findBrokerAddressOverride(b1.node_id_)).WillOnce(Return(r1));
  absl::optional<HostAndPort> r2 = absl::nullopt;
  EXPECT_CALL(*config, findBrokerAddressOverride(b2.node_id_)).WillOnce(Return(r2));
  absl::optional<HostAndPort> r3 = {{"nh3", 6666}};
  EXPECT_CALL(*config, findBrokerAddressOverride(b3.node_id_)).WillOnce(Return(r3));
  ResponseRewriterImpl testee{config};

  // when
  testee.updateMetadataBrokerAddresses(mr);

  // then
  assertAddress(mr.brokers_[0], r1->first, r1->second);
  assertAddress(mr.brokers_[1], b2.host_, b2.port_);
  assertAddress(mr.brokers_[2], r3->first, r3->second);
}

TEST(ResponseRewriterImplUnitTest, ShouldRewriteFindCoordinatorResponse) {
  // given

  FindCoordinatorResponse fcr = {0, 13, "host1", 1111};
  Coordinator c1 = {"k1", 1, "ch1", 2222, 0, {}, {}};
  Coordinator c2 = {"k2", 2, "ch2", 3333, 0, {}, {}};
  Coordinator c3 = {"k3", 3, "ch3", 4444, 0, {}, {}};
  fcr.coordinators_ = {c1, c2, c3};

  auto config = std::make_shared<MockBrokerFilterConfig>();
  absl::optional<HostAndPort> fcrhp = {{"nh1", 4444}};
  EXPECT_CALL(*config, findBrokerAddressOverride(fcr.node_id_)).WillOnce(Return(fcrhp));
  absl::optional<HostAndPort> cr1 = {{"nh1", 4444}};
  EXPECT_CALL(*config, findBrokerAddressOverride(c1.node_id_)).WillOnce(Return(cr1));
  absl::optional<HostAndPort> cr2 = absl::nullopt;
  EXPECT_CALL(*config, findBrokerAddressOverride(c2.node_id_)).WillOnce(Return(cr2));
  absl::optional<HostAndPort> cr3 = {{"nh3", 6666}};
  EXPECT_CALL(*config, findBrokerAddressOverride(c3.node_id_)).WillOnce(Return(cr3));
  ResponseRewriterImpl testee{config};

  // when
  testee.updateFindCoordinatorBrokerAddresses(fcr);

  // then
  assertAddress(fcr, fcrhp->first, fcrhp->second);
  assertAddress(fcr.coordinators_[0], cr1->first, cr1->second);
  assertAddress(fcr.coordinators_[1], c2.host_, c2.port_);
  assertAddress(fcr.coordinators_[2], cr3->first, cr3->second);
}

TEST(ResponseRewriterImplUnitTest, ShouldRewriteDescribeClusterResponse) {
  // given
  DescribeClusterBroker b1 = {13, "host1", 1111, absl::nullopt, {}};
  DescribeClusterBroker b2 = {42, "host2", 2222, absl::nullopt, {}};
  DescribeClusterBroker b3 = {77, "host3", 3333, absl::nullopt, {}};
  std::vector<DescribeClusterBroker> brokers = {b1, b2, b3};
  DescribeClusterResponse dcr = {0, 0, absl::nullopt, "", 0, brokers, 0, {}};

  auto config = std::make_shared<MockBrokerFilterConfig>();
  absl::optional<HostAndPort> cr1 = {{"nh1", 4444}};
  EXPECT_CALL(*config, findBrokerAddressOverride(b1.broker_id_)).WillOnce(Return(cr1));
  absl::optional<HostAndPort> cr2 = absl::nullopt;
  EXPECT_CALL(*config, findBrokerAddressOverride(b2.broker_id_)).WillOnce(Return(cr2));
  absl::optional<HostAndPort> cr3 = {{"nh3", 6666}};
  EXPECT_CALL(*config, findBrokerAddressOverride(b3.broker_id_)).WillOnce(Return(cr3));
  ResponseRewriterImpl testee{config};

  // when
  testee.updateDescribeClusterBrokerAddresses(dcr);

  // then
  assertAddress(dcr.brokers_[0], cr1->first, cr1->second);
  assertAddress(dcr.brokers_[1], b2.host_, b2.port_);
  assertAddress(dcr.brokers_[2], cr3->first, cr3->second);
}

TEST(ResponseRewriterUnitTest, ShouldCreateProperRewriter) {
  auto c1 = std::make_shared<MockBrokerFilterConfig>();
  EXPECT_CALL(*c1, needsResponseRewrite()).WillOnce(Return(true));
  ResponseRewriterSharedPtr r1 = createRewriter(c1);
  ASSERT_NE(std::dynamic_pointer_cast<ResponseRewriterImpl>(r1), nullptr);

  auto c2 = std::make_shared<MockBrokerFilterConfig>();
  EXPECT_CALL(*c2, needsResponseRewrite()).WillOnce(Return(false));
  ResponseRewriterSharedPtr r2 = createRewriter(c2);
  ASSERT_NE(std::dynamic_pointer_cast<DoNothingRewriter>(r2), nullptr);
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

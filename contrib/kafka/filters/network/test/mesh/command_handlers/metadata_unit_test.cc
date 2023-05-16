#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/metadata.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (InFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class MockUpstreamKafkaConfiguration : public UpstreamKafkaConfiguration {
public:
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic, (const std::string&),
              (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

TEST(MetadataTest, shouldBeAlwaysReadyForAnswer) {
  // given
  MockAbstractRequestListener filter;
  EXPECT_CALL(filter, onRequestReadyForAnswer());
  MockUpstreamKafkaConfiguration configuration;
  const std::pair<std::string, int32_t> advertised_address = {"host", 1234};
  EXPECT_CALL(configuration, getAdvertisedAddress()).WillOnce(Return(advertised_address));
  // First topic is going to have configuration present (42 partitions for each topic).
  const ClusterConfig topic1config = {"", 42, {}, {}};
  EXPECT_CALL(configuration, computeClusterConfigForTopic("topic1"))
      .WillOnce(Return(absl::make_optional(topic1config)));
  // Second topic is not going to have configuration present.
  EXPECT_CALL(configuration, computeClusterConfigForTopic("topic2"))
      .WillOnce(Return(absl::nullopt));
  const RequestHeader header = {METADATA_REQUEST_API_KEY, METADATA_REQUEST_MAX_VERSION, 0,
                                absl::nullopt};
  const MetadataRequestTopic t1 = MetadataRequestTopic{"topic1"};
  const MetadataRequestTopic t2 = MetadataRequestTopic{"topic2"};
  // Third topic is not going to have an explicit name.
  const MetadataRequestTopic t3 = MetadataRequestTopic{Uuid{13, 42}, absl::nullopt, TaggedFields{}};
  const MetadataRequest data = {{t1, t2, t3}};
  const auto message = std::make_shared<Request<MetadataRequest>>(header, data);
  MetadataRequestHolder testee = {filter, configuration, message};

  // when, then - invoking should immediately notify the filter.
  testee.startProcessing();

  // when, then - should always be considered finished.
  const bool finished = testee.finished();
  EXPECT_TRUE(finished);

  // when, then - the computed result is always contains correct data (confirmed by integration
  // tests).
  const auto answer = testee.computeAnswer();
  EXPECT_EQ(answer->metadata_.api_key_, header.api_key_);
  EXPECT_EQ(answer->metadata_.correlation_id_, header.correlation_id_);

  const auto response = std::dynamic_pointer_cast<Response<MetadataResponse>>(answer);
  ASSERT_TRUE(response);
  const auto topics = response->data_.topics_;
  EXPECT_EQ(topics.size(), 1);
  EXPECT_EQ(topics[0].name_, *(t1.name_));
  EXPECT_EQ(topics[0].partitions_.size(), 42);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

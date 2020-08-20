#include "extensions/filters/network/kafka/mesh/abstract_command.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/metadata.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"
#include "extensions/filters/network/kafka/mesh/splitter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (AbstractInFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
};

class MockClusteringConfiguration : public ClusteringConfiguration {
public:
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic, (const std::string&),
              (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

TEST(RequestProcessorTest, shouldProcessProduceRequest) {
  // given
  MockAbstractRequestListener listener;
  MockClusteringConfiguration configuration;
  RequestProcessor testee = {listener, configuration};

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);

  AbstractInFlightRequestSharedPtr capture = nullptr;
  EXPECT_CALL(listener, onRequest(_)).WillOnce(testing::SaveArg<0>(&capture));

  // when
  testee.onMessage(message);

  // then
  ASSERT_NE(std::dynamic_pointer_cast<ProduceRequestHolder>(capture), nullptr);
}

TEST(RequestProcessorTest, shouldProcessMetadataRequest) {
  // given
  MockAbstractRequestListener listener;
  MockClusteringConfiguration configuration;
  RequestProcessor testee = {listener, configuration};

  const RequestHeader header = {3, 0, 0, absl::nullopt};
  const MetadataRequest data = {absl::nullopt};
  const auto message = std::make_shared<Request<MetadataRequest>>(header, data);

  AbstractInFlightRequestSharedPtr capture = nullptr;
  EXPECT_CALL(listener, onRequest(_)).WillOnce(testing::SaveArg<0>(&capture));

  // when
  testee.onMessage(message);

  // then
  ASSERT_NE(std::dynamic_pointer_cast<MetadataRequestHolder>(capture), nullptr);
}

TEST(RequestProcessorTest, shouldProcessApiVersionsRequest) {
  // given
  MockAbstractRequestListener listener;
  MockClusteringConfiguration configuration;
  RequestProcessor testee = {listener, configuration};

  const RequestHeader header = {18, 0, 0, absl::nullopt};
  const ApiVersionsRequest data = {};
  const auto message = std::make_shared<Request<ApiVersionsRequest>>(header, data);

  AbstractInFlightRequestSharedPtr capture = nullptr;
  EXPECT_CALL(listener, onRequest(_)).WillOnce(testing::SaveArg<0>(&capture));

  // when
  testee.onMessage(message);

  // then
  ASSERT_NE(std::dynamic_pointer_cast<ApiVersionsRequestHolder>(capture), nullptr);
}

} // anonymous namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

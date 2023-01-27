#include "contrib/kafka/filters/network/source/mesh/command_handlers/api_versions.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

TEST(ApiVersionsTest, shouldBeAlwaysReadyForAnswer) {
  // given
  MockAbstractRequestListener filter;
  EXPECT_CALL(filter, onRequestReadyForAnswer());
  const RequestHeader header = {API_VERSIONS_REQUEST_API_KEY, 0, 0, absl::nullopt};
  ApiVersionsRequestHolder testee = {filter, header};

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
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

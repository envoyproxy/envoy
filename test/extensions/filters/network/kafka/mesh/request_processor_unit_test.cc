#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"
#include "source/extensions/filters/network/kafka/mesh/request_processor.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class RequestProcessorTest : public testing::Test {
protected:
  RequestProcessor testee_ = {};
};

TEST_F(RequestProcessorTest, ShouldHandleUnsupportedRequest) {
  // given
  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ListOffsetRequest data = {0, {}};
  const auto message = std::make_shared<Request<ListOffsetRequest>>(header, data);

  // when, then - exception gets thrown.
  EXPECT_THROW_WITH_REGEX(testee_.onMessage(message), EnvoyException, "unsupported");
}

TEST_F(RequestProcessorTest, ShouldHandleUnparseableRequest) {
  // given
  const RequestHeader header = {42, 42, 42, absl::nullopt};
  const auto arg = std::make_shared<RequestParseFailure>(header);

  // when, then - exception gets thrown.
  EXPECT_THROW_WITH_REGEX(testee_.onFailedParse(arg), EnvoyException, "unknown");
}

} // anonymous namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

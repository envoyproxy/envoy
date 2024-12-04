#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"

#include "contrib/kafka/filters/network/source/broker/request_handler.h"
#include "contrib/kafka/filters/network/test/broker/mock_filter_config.h"
#include "contrib/kafka/filters/network/test/broker/mock_request.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class RequestHandlerImplUnitTest : public testing::Test {
protected:
  MockBrokerFilterConfigSharedPtr config_ = std::make_shared<MockBrokerFilterConfig>();

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  RequestHandlerImpl createTestee() {
    RequestHandlerImpl result{config_};
    result.setReadFilterCallbacks(filter_callbacks_);
    return result;
  }
};

TEST_F(RequestHandlerImplUnitTest, ShouldDoNothing) {
  // given
  const absl::flat_hash_set<int16_t> ret = {};
  EXPECT_CALL(*config_, apiKeysAllowed).WillRepeatedly(Return(ret));
  EXPECT_CALL(*config_, apiKeysDenied).WillRepeatedly(Return(ret));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);

  RequestHandlerImpl testee = createTestee();
  MockRequestSharedPtr request = std::make_shared<MockRequest>(0, 0, 0);

  // when
  testee.onMessage(request);

  // then - expectations fulfilled (connection untouched).
}

TEST_F(RequestHandlerImplUnitTest, ShouldCloseConnection) {
  // given
  const absl::flat_hash_set<int16_t> allowed = {};
  const absl::flat_hash_set<int16_t> rejected = {13};
  EXPECT_CALL(*config_, apiKeysAllowed).WillRepeatedly(Return(allowed));
  EXPECT_CALL(*config_, apiKeysDenied).WillRepeatedly(Return(rejected));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite, _));

  RequestHandlerImpl testee = createTestee();
  MockRequestSharedPtr request = std::make_shared<MockRequest>(13, 0, 0);

  // when
  testee.onMessage(request);

  // then - expectations fulfilled (connection closed).
}

TEST(RequestHandlerImplApiKeyAllowed, ShouldGiveCorrectResults) {
  ASSERT_TRUE(RequestHandlerImpl::apiKeyAllowed(13, {}, {})); // No config.

  ASSERT_TRUE(RequestHandlerImpl::apiKeyAllowed(13, {13, 42}, {}));     // Allowed.
  ASSERT_TRUE(RequestHandlerImpl::apiKeyAllowed(13, {}, {0, 1, 2, 3})); // Not denied.
  ASSERT_TRUE(
      RequestHandlerImpl::apiKeyAllowed(13, {13, 42}, {0, 1, 2, 3})); // Allowed + not denied.

  ASSERT_FALSE(RequestHandlerImpl::apiKeyAllowed(13, {}, {13, 42}));     // Denied.
  ASSERT_FALSE(RequestHandlerImpl::apiKeyAllowed(13, {0, 1, 2, 3}, {})); // Not allowed.
  ASSERT_FALSE(
      RequestHandlerImpl::apiKeyAllowed(13, {0, 1, 2, 3}, {13, 42})); // Not allowed + denied.
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

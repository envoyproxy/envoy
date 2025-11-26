#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/connection_pool_cardinality/filter.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

class FilterTest : public testing::Test {
public:
  FilterTest() {
    ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::FilterChain);
    ON_CALL(stream_info_, filterState()).WillByDefault(ReturnRef(filter_state_));
  }

  Filter init(uint32_t connection_pool_count) {
    auto filter = Filter{connection_pool_count, random_};
    filter.setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  Envoy::Http::TestRequestHeaderMapImpl request_headers_;
  StreamInfo::FilterStateSharedPtr filter_state_;
};

TEST_F(FilterTest, NoConnectionPoolSelection) {
  InSequence s;

  // With pool count of 1, no dynamic state should be set
  auto filter = init(1);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers_, false));
}

TEST_F(FilterTest, SingleConnectionPoolSelection) {
  InSequence s;

  // With pool count of 0, should default to 1 and not set dynamic state
  auto filter = init(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers_, false));
}

TEST_F(FilterTest, MultipleConnectionPoolSelection) {
  InSequence s;

  const uint32_t pool_count = 4;
  const uint64_t random_value = 42;
  const uint32_t expected_pool_index = random_value % pool_count; // 42 % 4 = 2

  EXPECT_CALL(random_, random()).WillOnce(Return(random_value));

  auto filter = init(pool_count);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers_, false));

  // Verify that the filter state contains the connection pool data
  const auto* conn_pool_data =
      filter_state_->getDataReadOnly<ConnPoolCardinality>("connection_pool_cardinality");
  ASSERT_NE(conn_pool_data, nullptr);
  EXPECT_EQ(expected_pool_index, conn_pool_data->idx_);
}

TEST_F(FilterTest, LargePoolCountSelection) {
  InSequence s;

  const uint32_t pool_count = 100;
  const uint64_t random_value = 12345;
  const uint32_t expected_pool_index = random_value % pool_count; // 12345 % 100 = 45

  EXPECT_CALL(random_, random()).WillOnce(Return(random_value));

  auto filter = init(pool_count);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers_, false));

  // Verify that the filter state contains the connection pool data
  const auto* conn_pool_data =
      filter_state_->getDataReadOnly<ConnPoolCardinality>("connection_pool_cardinality");
  ASSERT_NE(conn_pool_data, nullptr);
  EXPECT_EQ(expected_pool_index, conn_pool_data->idx_);
}

TEST_F(FilterTest, EdgeCaseMaxRandomValue) {
  InSequence s;

  const uint32_t pool_count = 3;
  const uint64_t random_value = UINT64_MAX;
  const uint32_t expected_pool_index =
      random_value % pool_count; // Should work correctly even with max value

  EXPECT_CALL(random_, random()).WillOnce(Return(random_value));

  auto filter = init(pool_count);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter.decodeHeaders(request_headers_, false));

  // Verify that the filter state contains the connection pool data
  const auto* conn_pool_data =
      filter_state_->getDataReadOnly<ConnPoolCardinality>("connection_pool_cardinality");
  ASSERT_NE(conn_pool_data, nullptr);
  EXPECT_EQ(expected_pool_index, conn_pool_data->idx_);
}

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

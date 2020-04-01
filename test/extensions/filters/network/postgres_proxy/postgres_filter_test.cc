#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tuple>

#include "extensions/filters/network/postgres_proxy/postgres_filter.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

using ::testing::WithArg;
using ::testing::WithArgs;

class DecoderTest : public Decoder {
public:
  MOCK_METHOD(bool, onData, (Buffer::Instance&, bool), (override));
  MOCK_METHOD(PostgresSession&, getSession, (), (override));
};

class PostgresFilterTest
    : public ::testing::TestWithParam<
          std::tuple<std::function<void(PostgresFilter*, Buffer::Instance&, bool)>,
                     std::function<uint32_t(const PostgresFilter*)>>> {
public:
  PostgresFilterTest() {
    config_ = std::make_shared<PostgresFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<PostgresFilter>(config_);

    filter_->setDecoder(std::make_unique<DecoderTest>());
  }

  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test."};
  std::unique_ptr<PostgresFilter> filter_;
  PostgresFilterConfigSharedPtr config_;

  // These variables are used internally in tests
  Buffer::OwnedImpl data_;
  char buf_[256];
};

TEST_F(PostgresFilterTest, NewConnection) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
}

// Test reading buffer until the buffer is exhausted
// or decoder indicates that there is not enough data in a buffer
// to process a message.
TEST_P(PostgresFilterTest, ReadData) {
  data_.add(buf_, 256);

  // Simulate reading entire buffer.
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(data.length());
        return true;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading entire data in two steps
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(100);
        return true;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(156);
        return true;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading 3 packets. The first two were processed correctly and
  // for the third one there was not enough data. There should be 56 bytes
  // of unprocessed data.
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(100);
        return true;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(100);
        return true;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(0);
        return false;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 56);
}

// Parameterized test:
// First value in the tuple is method taking buffer with received data.
// Second value in the tuple is method returning how many bytes are left after processing.
INSTANTIATE_TEST_SUITE_P(ProcessDataTests, PostgresFilterTest,
                         ::testing::Values(std::make_tuple(&PostgresFilter::onData,
                                                           &PostgresFilter::getFrontendBufLength),
                                           std::make_tuple(&PostgresFilter::onWrite,
                                                           &PostgresFilter::getBackendBufLength)));

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

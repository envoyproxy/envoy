#include "extensions/filters/network/postgresql_proxy/postgresql_filter.h"

#include "test/mocks/network/mocks.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tuple>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

using ::testing::WithArg;

class DecoderTest : public Decoder {
public:
  MOCK_METHOD(bool, onData, (Buffer::Instance&), (override));
  MOCK_METHOD(void, onFrontendData, (Buffer::Instance&), (override));
  MOCK_METHOD(PostgreSQLSession&,  getSession, (), (override));
};

class PostgreSQLFilterTest : public ::testing::TestWithParam< std::tuple<std::function<void(PostgreSQLFilter*, Buffer::Instance&, bool)>,
  std::function<uint32_t(const PostgreSQLFilter*)> > >{
public:
  PostgreSQLFilterTest() {
    config_ = std::make_shared<PostgreSQLFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<PostgreSQLFilter>(config_);

    filter_->setDecoder(std::make_unique<DecoderTest>());
  	}

  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test."};
  std::unique_ptr<PostgreSQLFilter> filter_;
  PostgreSQLFilterConfigSharedPtr config_;

  // These variables are used internally in tests 
  Buffer::OwnedImpl data_;
  char buf_[256];
};

TEST_F(PostgreSQLFilterTest, NewConnection) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection()); 
}

// Test reading buffer until the buffer is exhausted
// or decoder indicates that there is not enough data in a buffer
// to process a message.
TEST_P(PostgreSQLFilterTest, readData) {
  data_.add(buf_, 256);

  // Simulate reading entire buffer.
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData).
		  WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(data.length());return true;})));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading entire data in two steps
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData)
		  .WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(100); return true;})))
		  .WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(156); return true;})));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading 3 packates. The first two were processed correclty and 
  // for the third one there was not enough data. There should be 56 bytes 
  // of unprocessed data.
  EXPECT_CALL(*(reinterpret_cast<DecoderTest*>(filter_->getDecoder())), onData)
		  .WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(100); return true;})))
		  .WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(100); return true;})))
		  .WillOnce(WithArg<0>(Invoke([](Buffer::Instance& data)->bool {data.drain(0); return false;})));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 56);
}

// Parameterized test:
// First value in the tupple is method taking buffer with received data.
// Second value in the tupple is methor returning how many bytes are left after processing.
INSTANTIATE_TEST_CASE_P(
  ProcessDataTests,
  PostgreSQLFilterTest,
  ::testing::Values(
	  std::make_tuple(&PostgreSQLFilter::onData, &PostgreSQLFilter::getFrontendBufLength),
	  std::make_tuple(&PostgreSQLFilter::onWrite, &PostgreSQLFilter::getBackendBufLength)
  ));

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

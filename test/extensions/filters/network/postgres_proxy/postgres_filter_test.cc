#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tuple>

#include "extensions/filters/network/postgres_proxy/postgres_filter.h"

#include "test/extensions/filters/network/postgres_proxy/postgres_test_utils.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

using ::testing::WithArgs;

// Decoder mock.
class MockDecoderTest : public Decoder {
public:
  MOCK_METHOD(bool, onData, (Buffer::Instance&, bool), (override));
  MOCK_METHOD(PostgresSession&, getSession, (), (override));
};

// Fixture class.
class PostgresFilterTest
    : public ::testing::TestWithParam<
          std::tuple<std::function<void(PostgresFilter*, Buffer::Instance&, bool)>,
                     std::function<uint32_t(const PostgresFilter*)>>> {
public:
  PostgresFilterTest() {
    config_ = std::make_shared<PostgresFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<PostgresFilter>(config_);

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test."};
  std::unique_ptr<PostgresFilter> filter_;
  PostgresFilterConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  // These variables are used internally in tests.
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
  // Create mock decoder, obtain raw pointer to it (required for EXPECT_CALL)
  // and pass the decoder to filter.
  std::unique_ptr<MockDecoderTest> decoder = std::make_unique<MockDecoderTest>();
  MockDecoderTest* decoderPtr = decoder.get();
  filter_->setDecoder(std::move(decoder));

  data_.add(buf_, 256);

  // Simulate reading entire buffer.
  EXPECT_CALL(*decoderPtr, onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> bool {
        data.drain(data.length());
        return true;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading entire data in two steps.
  EXPECT_CALL(*decoderPtr, onData)
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
  EXPECT_CALL(*decoderPtr, onData)
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

// Test generates various postgres payloads and feeds them into filter.
// It expects that certain statistics are updated.
TEST_F(PostgresFilterTest, BackendMsgsStats) {
  // pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->setStartup(false);

  // unknown message
  createPostgresMsg(data_, "=", "blah blah blah");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().messages_unknown_.value(), 1);

  filter_->getDecoder()->getSession().setInTransaction(true);
  createPostgresMsg(data_, "C", "COMMIT");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 0);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 1);

  createPostgresMsg(data_, "C", "ROLLBACK 234");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 0);
  ASSERT_THAT(filter_->getStats().statements_.value(), 2);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 0);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 1);

  createPostgresMsg(data_, "C", "SELECT blah");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 3);
  ASSERT_THAT(filter_->getStats().statements_select_.value(), 1);

  createPostgresMsg(data_, "C", "INSERT 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 4);
  ASSERT_THAT(filter_->getStats().statements_insert_.value(), 1);

  createPostgresMsg(data_, "C", "DELETE 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 5);
  ASSERT_THAT(filter_->getStats().statements_delete_.value(), 1);

  createPostgresMsg(data_, "C", "UPDATE 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 6);
  ASSERT_THAT(filter_->getStats().statements_update_.value(), 1);

  createPostgresMsg(data_, "C", "BEGIN 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 7);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 1);
}

// Test sends series of E type error messages to the filter and
// verifies that statistic counters are increased.
TEST_F(PostgresFilterTest, ErrorMsgsStats) {
  // Pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->setStartup(false);

  createPostgresMsg(data_, "E", "SERRORVERRORC22012");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().errors_.value(), 1);
  ASSERT_THAT(filter_->getStats().errors_error_.value(), 1);

  createPostgresMsg(data_, "E", "SFATALVFATALC22012");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().errors_.value(), 2);
  ASSERT_THAT(filter_->getStats().errors_fatal_.value(), 1);

  createPostgresMsg(data_, "E", "SPANICVPANICC22012");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().errors_.value(), 3);
  ASSERT_THAT(filter_->getStats().errors_panic_.value(), 1);

  createPostgresMsg(data_, "E", "SBLAHBLAHC22012");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().errors_.value(), 4);
  ASSERT_THAT(filter_->getStats().errors_unknown_.value(), 1);
}

// Test sends series of N type messages to the filter and verifies
// that corresponding stats counters are updated.
TEST_F(PostgresFilterTest, NoticeMsgsStats) {
  // Pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->setStartup(false);

  createPostgresMsg(data_, "N", "SblalalaC2345");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 1);
  ASSERT_THAT(filter_->getStats().notices_unknown_.value(), 1);

  createPostgresMsg(data_, "N", "SblahVWARNING23345");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 2);
  ASSERT_THAT(filter_->getStats().notices_warning_.value(), 1);

  createPostgresMsg(data_, "N", "SNOTICEERRORbbal4");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 3);
  ASSERT_THAT(filter_->getStats().notices_notice_.value(), 1);

  createPostgresMsg(data_, "N", "SINFOVblabla");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 4);
  ASSERT_THAT(filter_->getStats().notices_info_.value(), 1);

  createPostgresMsg(data_, "N", "SDEBUGDEBUG");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 5);
  ASSERT_THAT(filter_->getStats().notices_debug_.value(), 1);

  createPostgresMsg(data_, "N", "SLOGGGGINFO");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().notices_.value(), 6);
  ASSERT_THAT(filter_->getStats().notices_log_.value(), 1);
}

// Encrypted sessions are detected based on the first received message.
TEST_F(PostgresFilterTest, EncryptedSessionStats) {
  data_.writeBEInt<uint32_t>(8);
  // 1234 in the most significant 16 bits and some code in the least significant 16 bits.
  data_.writeBEInt<uint32_t>(80877103); // SSL code.
  filter_->onData(data_, false);
  ASSERT_THAT(filter_->getStats().sessions_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_encrypted_.value(), 1);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

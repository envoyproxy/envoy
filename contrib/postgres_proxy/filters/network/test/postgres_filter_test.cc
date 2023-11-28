#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tuple>

#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"

#include "contrib/postgres_proxy/filters/network/source/postgres_filter.h"
#include "contrib/postgres_proxy/filters/network/test/postgres_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

using testing::ReturnRef;
using ::testing::WithArgs;

// Decoder mock.
class MockDecoderTest : public Decoder {
public:
  MOCK_METHOD(Decoder::Result, onData, (Buffer::Instance&, bool), (override));
  MOCK_METHOD(PostgresSession&, getSession, (), (override));
};

// Fixture class.
class PostgresFilterTest
    : public ::testing::TestWithParam<
          std::tuple<std::function<void(PostgresFilter*, Buffer::Instance&, bool)>,
                     std::function<uint32_t(const PostgresFilter*)>>> {
public:
  PostgresFilterTest() {

    PostgresFilterConfig::PostgresFilterConfigOptions config_options{
        stat_prefix_, true, false,
        envoy::extensions::filters::network::postgres_proxy::v3alpha::
            PostgresProxy_SSLMode_DISABLE};

    config_ = std::make_shared<PostgresFilterConfig>(config_options, scope_);
    filter_ = std::make_unique<PostgresFilter>(config_);

    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void setMetadata() {
    EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    ON_CALL(stream_info_, setDynamicMetadata(NetworkFilterNames::get().PostgresProxy, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          stream_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
                  NetworkFilterNames::get().PostgresProxy, obj));
        }));
  }

  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::string stat_prefix_{"test."};
  std::unique_ptr<PostgresFilter> filter_;
  PostgresFilterConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

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
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(data.length());
        return Decoder::Result::ReadyForNext;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading entire data in two steps.
  EXPECT_CALL(*decoderPtr, onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(100);
        return Decoder::Result::ReadyForNext;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(156);
        return Decoder::Result::ReadyForNext;
      })));
  std::get<0>(GetParam())(filter_.get(), data_, false);
  ASSERT_THAT(std::get<1>(GetParam())(filter_.get()), 0);

  // Simulate reading 3 packets. The first two were processed correctly and
  // for the third one there was not enough data. There should be 56 bytes
  // of unprocessed data.
  EXPECT_CALL(*decoderPtr, onData)
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(100);
        return Decoder::Result::ReadyForNext;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(100);
        return Decoder::Result::ReadyForNext;
      })))
      .WillOnce(WithArgs<0, 1>(Invoke([](Buffer::Instance& data, bool) -> Decoder::Result {
        data.drain(0);
        return Decoder::Result::NeedMoreData;
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
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InSyncState);

  // unknown message
  createPostgresMsg(data_, "=", "blah blah blah");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().messages_unknown_.value(), 1);

  // implicit transactions
  createPostgresMsg(data_, "C", "SELECT blah");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 1);
  ASSERT_THAT(filter_->getStats().statements_select_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "INSERT 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 2);
  ASSERT_THAT(filter_->getStats().statements_insert_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 2);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 2);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "DELETE 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 3);
  ASSERT_THAT(filter_->getStats().statements_delete_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 3);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 3);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "UPDATE 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 4);
  ASSERT_THAT(filter_->getStats().statements_update_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 4);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 4);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  // explicit transactions (commit)
  createPostgresMsg(data_, "C", "BEGIN 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 5);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 4);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "INSERT 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 6);
  ASSERT_THAT(filter_->getStats().statements_insert_.value(), 2);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 4);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "COMMIT");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 7);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 2);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  // explicit transactions (rollback)
  createPostgresMsg(data_, "C", "BEGIN 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 8);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 3);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 6);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "INSERT 123");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 9);
  ASSERT_THAT(filter_->getStats().statements_insert_.value(), 3);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 6);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);

  createPostgresMsg(data_, "C", "ROLLBACK");
  filter_->onWrite(data_, false);
  ASSERT_THAT(filter_->getStats().statements_.value(), 10);
  ASSERT_THAT(filter_->getStats().statements_other_.value(), 4);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 6);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 5);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 1);
}

// Test sends series of E type error messages to the filter and
// verifies that statistic counters are increased.
TEST_F(PostgresFilterTest, ErrorMsgsStats) {
  // Pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InSyncState);

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
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InSyncState);

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
  data_.writeBEInt<uint32_t>(80877104); // SSL code.
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, true));
  ASSERT_THAT(filter_->getStats().sessions_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_encrypted_.value(), 1);
}

// Test verifies that incorrect SQL statement does not create
// Postgres metadata.
TEST_F(PostgresFilterTest, MetadataIncorrectSQL) {
  // Pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InSyncState);
  setMetadata();

  createPostgresMsg(data_, "Q", "BLAH blah blah");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, true));

  // SQL statement was wrong. No metadata should have been created.
  ASSERT_THAT(filter_->connection().streamInfo().dynamicMetadata().filter_metadata().contains(
                  NetworkFilterNames::get().PostgresProxy),
              false);
  ASSERT_THAT(filter_->getStats().statements_parse_error_.value(), 1);
  ASSERT_THAT(filter_->getStats().statements_parsed_.value(), 0);
}

// Test verifies that Postgres metadata is created for correct SQL statement.
// and it happens only when parse_sql flag is true.
TEST_F(PostgresFilterTest, QueryMessageMetadata) {
  // Pretend that startup message has been received.
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InSyncState);
  setMetadata();

  // Disable creating parsing SQL and creating metadata.
  filter_->getConfig()->enable_sql_parsing_ = false;
  createPostgresMsg(data_, "Q", "SELECT * FROM whatever");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));

  ASSERT_THAT(filter_->connection().streamInfo().dynamicMetadata().filter_metadata().contains(
                  NetworkFilterNames::get().PostgresProxy),
              false);
  ASSERT_THAT(filter_->getStats().statements_parse_error_.value(), 0);
  ASSERT_THAT(filter_->getStats().statements_parsed_.value(), 0);

  // Now enable SQL parsing and creating metadata.
  filter_->getConfig()->enable_sql_parsing_ = true;
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));

  auto& filter_meta = filter_->connection().streamInfo().dynamicMetadata().filter_metadata().at(
      NetworkFilterNames::get().PostgresProxy);
  auto& fields = filter_meta.fields();

  ASSERT_THAT(fields.size(), 1);
  ASSERT_THAT(fields.contains("whatever"), true);

  const auto& operations = fields.at("whatever").list_value();
  ASSERT_EQ("select", operations.values(0).string_value());

  ASSERT_THAT(filter_->getStats().statements_parse_error_.value(), 0);
  ASSERT_THAT(filter_->getStats().statements_parsed_.value(), 1);
}

// Test verifies that filter reacts to RequestSSL message.
// It should reply with "S" message and OnData should return
// Decoder::Stopped.
TEST_F(PostgresFilterTest, TerminateSSL) {
  filter_->getConfig()->terminate_ssl_ = true;
  EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));
  Buffer::OwnedImpl buf;
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
      .WillOnce(testing::SaveArg<0>(&buf));
  data_.writeBEInt<uint32_t>(8);
  // 1234 in the most significant 16 bits and some code in the least significant 16 bits.
  data_.writeBEInt<uint32_t>(80877103); // SSL code.
  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onData(data_, true));
  ASSERT_THAT('S', buf.peekBEInt<char>(0));
  ASSERT_THAT(filter_->getStats().messages_.value(), 1);
  ASSERT_THAT(filter_->getStats().messages_frontend_.value(), 1);

  // Now indicate through the callback that 1 bytes has been sent.
  // Filter should call startSecureTransport and should not close the connection.
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_CALL(connection_, close(_)).Times(0);
  cb(1);
  // Verify stats. This should not count as encrypted or unencrypted session.
  ASSERT_THAT(filter_->getStats().sessions_terminated_ssl_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_encrypted_.value(), 0);
  ASSERT_THAT(filter_->getStats().sessions_unencrypted_.value(), 0);

  // Call callback again, but this time indicate that startSecureTransport failed.
  // Filter should close the connection.
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(false));
  EXPECT_CALL(connection_, close(_));
  cb(1);
  ASSERT_THAT(filter_->getStats().sessions_terminated_ssl_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_encrypted_.value(), 0);
  ASSERT_THAT(filter_->getStats().sessions_unencrypted_.value(), 0);
}

TEST_F(PostgresFilterTest, UpstreamSSL) {
  EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  // Configure upstream SSL to be disabled. encryptUpstream must not be called.
  filter_->getConfig()->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::DISABLE;
  ASSERT_FALSE(filter_->shouldEncryptUpstream());
  ASSERT_DEATH(filter_->encryptUpstream(true, data_), ".*");
  ASSERT_DEATH(filter_->encryptUpstream(false, data_), ".*");

  // Configure upstream SSL to be required. If upstream server does not agree for SSL or
  // converting upstream transport socket to secure mode fails, the filter should bump
  // proper stats and close the connection to downstream client.
  filter_->getConfig()->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  ASSERT_TRUE(filter_->shouldEncryptUpstream());
  // Simulate that upstream server agreed for SSL and conversion of upstream Transport socket was
  // successful.
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));
  filter_->encryptUpstream(true, data_);
  ASSERT_EQ(1, filter_->getStats().sessions_upstream_ssl_success_.value());
  // Simulate that upstream server agreed for SSL but conversion of upstream Transport socket
  // failed.
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(false));
  filter_->encryptUpstream(true, data_);
  ASSERT_EQ(1, filter_->getStats().sessions_upstream_ssl_failed_.value());
  // Simulate that upstream server does not agree for SSL. Filter should close the connection to
  // downstream client.
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).Times(0);
  EXPECT_CALL(connection_, close(_));
  filter_->encryptUpstream(false, data_);
  ASSERT_EQ(2, filter_->getStats().sessions_upstream_ssl_failed_.value());
}

TEST_F(PostgresFilterTest, UpstreamSSLStats) {
  static_cast<DecoderImpl*>(filter_->getDecoder())->state(DecoderImpl::State::InitState);
  EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  filter_->getConfig()->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;

  createInitialPostgresRequest(data_);
  filter_->onData(data_, false);

  Buffer::OwnedImpl upstream_data;
  upstream_data.add("S");
  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));
  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onWrite(upstream_data, false));

  createPostgresMsg(upstream_data, "C", "SELECT blah");
  filter_->onWrite(upstream_data, false);
  ASSERT_THAT(filter_->getStats().sessions_upstream_ssl_success_.value(), 1);
  ASSERT_THAT(filter_->getStats().statements_.value(), 1);
  ASSERT_THAT(filter_->getStats().statements_select_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_commit_.value(), 1);
  ASSERT_THAT(filter_->getStats().transactions_rollback_.value(), 0);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

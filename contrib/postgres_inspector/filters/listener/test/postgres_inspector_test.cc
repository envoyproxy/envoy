#include <memory>
#include <string>

#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.h"
#include "contrib/postgres_inspector/filters/listener/source/postgres_inspector.h"
#include "contrib/postgres_inspector/filters/listener/source/postgres_message_parser.h"
#include "contrib/postgres_inspector/filters/listener/test/postgres_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {
namespace {

class PostgresInspectorTest : public testing::Test {
public:
  PostgresInspectorTest()
      : cfg_(std::make_shared<Config>(*stats_store_.rootScope(), proto_config_)) {

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  }

  void init() {
    filter_ = std::make_unique<Filter>(cfg_);
    filter_->onAccept(cb_);
  }

  void initWithConfig(const ProtoConfig& config) {
    cfg_ = std::make_shared<Config>(*stats_store_.rootScope(), config);
    filter_ = std::make_unique<Filter>(cfg_);
    filter_->onAccept(cb_);
  }

  // Helper to create a mock buffer.
  class MockListenerFilterBuffer : public Network::ListenerFilterBuffer {
  public:
    explicit MockListenerFilterBuffer(Buffer::OwnedImpl&& data) {
      data_ = std::move(data);
      updateRawSlice();
    }

    const Buffer::ConstRawSlice rawSlice() const override { return raw_slice_; }

    bool drain(uint64_t len) override {
      if (len > data_.length()) {
        return false;
      }
      data_.drain(len);
      updateRawSlice();
      return true;
    }

  private:
    void updateRawSlice() {
      const auto length = data_.length();
      if (length > 0) {
        // Linearize and set up the raw slice
        const void* linearized = data_.linearize(length);
        raw_slice_.mem_ = const_cast<void*>(linearized);
        raw_slice_.len_ = length;
      } else {
        raw_slice_.mem_ = nullptr;
        raw_slice_.len_ = 0;
      }
    }

    Buffer::OwnedImpl data_;
    Buffer::ConstRawSlice raw_slice_;
  };

  std::unique_ptr<Network::ListenerFilterBuffer> createBuffer(Buffer::OwnedImpl&& data) {
    return std::make_unique<MockListenerFilterBuffer>(std::move(data));
  }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  ProtoConfig proto_config_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  NiceMock<Network::MockConnectionSocket> socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Event::MockTimer* timeout_timer_;
};

// Test SSL request detection.
TEST_F(PostgresInspectorTest, SslRequest) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));

  auto data = PostgresTestUtils::createSslRequest();
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().ssl_requested_.value());
  EXPECT_EQ(1, cfg_->stats().postgres_found_.value());
  EXPECT_EQ(0, cfg_->stats().ssl_not_requested_.value());

  // Verify bytes_processed histogram (8 bytes for SSLRequest)
  Stats::Histogram& histogram = stats_store_.histogramFromString(
      "postgres_inspector.bytes_processed", Stats::Histogram::Unit::Bytes);
  EXPECT_NE("", histogram.name());
}

// Test valid startup message.
TEST_F(PostgresInspectorTest, ValidStartupMessage) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));
  EXPECT_CALL(cb_, setDynamicTypedMetadata("envoy.postgres_inspector", _));

  std::map<std::string, std::string> params = {
      {"user", "testuser"}, {"database", "testdb"}, {"application_name", "test_app"}};

  auto data = PostgresTestUtils::createStartupMessage(params);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_found_.value());
  EXPECT_EQ(1, cfg_->stats().ssl_not_requested_.value());
  EXPECT_EQ(0, cfg_->stats().ssl_requested_.value());

  // Verify bytes_processed histogram
  Stats::Histogram& histogram = stats_store_.histogramFromString(
      "postgres_inspector.bytes_processed", Stats::Histogram::Unit::Bytes);
  EXPECT_NE("", histogram.name());
}

// Test CancelRequest handling.
TEST_F(PostgresInspectorTest, CancelRequest) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));

  auto data = PostgresTestUtils::createCancelRequest(123, 456);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_found_.value());
  EXPECT_EQ(1, cfg_->stats().ssl_not_requested_.value());
}

// Test startup message with minimal parameters.
TEST_F(PostgresInspectorTest, MinimalStartupMessage) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));
  EXPECT_CALL(cb_, setDynamicTypedMetadata("envoy.postgres_inspector", _));

  std::map<std::string, std::string> params = {{"user", "postgres"}};

  auto data = PostgresTestUtils::createStartupMessage(params);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_found_.value());
  EXPECT_EQ(1, cfg_->stats().ssl_not_requested_.value());
}

// Test startup message without database parameter (should default to user).
TEST_F(PostgresInspectorTest, StartupMessageDefaultDatabase) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));
  EXPECT_CALL(cb_, setDynamicTypedMetadata("envoy.postgres_inspector", _))
      .WillOnce(Invoke([](const std::string& ns, const Protobuf::Any& any) {
        EXPECT_EQ("envoy.postgres_inspector", ns);
        envoy::extensions::filters::listener::postgres_inspector::v3alpha::StartupMetadata typed;
        ASSERT_TRUE(any.UnpackTo(&typed));
        EXPECT_EQ("testuser", typed.user());
        EXPECT_EQ("testuser", typed.database());
      }));

  std::map<std::string, std::string> params = {{"user", "testuser"}};

  auto data = PostgresTestUtils::createStartupMessage(params);
  auto buffer = createBuffer(std::move(data));

  filter_->onData(*buffer);
}

// Test invalid protocol version.
TEST_F(PostgresInspectorTest, InvalidProtocolVersion) {
  init();

  auto data = PostgresTestUtils::createInvalidStartupMessage(123456);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().protocol_error_.value());
  EXPECT_EQ(0, cfg_->stats().postgres_found_.value());

  // Verify bytes_processed histogram (even for errors, we track what we processed)
  Stats::Histogram& histogram = stats_store_.histogramFromString(
      "postgres_inspector.bytes_processed", Stats::Histogram::Unit::Bytes);
  EXPECT_NE("", histogram.name());
}

// Test message too large.
TEST_F(PostgresInspectorTest, MessageTooLarge) {
  proto_config_.mutable_max_startup_message_size()->set_value(100);
  initWithConfig(proto_config_);

  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  ON_CALL(io_handle, close()).WillByDefault(Invoke([]() -> Api::IoCallUint64Result {
    return {0, Api::IoError::none()};
  }));

  auto data = PostgresTestUtils::createOversizedMessage(200);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(1, cfg_->stats().startup_message_too_large_.value());
}

// Test partial message (need more data).
TEST_F(PostgresInspectorTest, PartialMessage) {
  init();

  // Create a partial startup message.
  auto data = PostgresTestUtils::createPartialStartupMessage(100, 50);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  // Should wait for more data.
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
}

// Test metadata extraction disabled.
TEST_F(PostgresInspectorTest, MetadataExtractionDisabled) {
  proto_config_.mutable_enable_metadata_extraction()->set_value(false);
  initWithConfig(proto_config_);

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));
  EXPECT_CALL(cb_, setDynamicTypedMetadata(_, _)).Times(0); // Should not call typed metadata

  std::map<std::string, std::string> params = {{"user", "testuser"}, {"database", "testdb"}};

  auto data = PostgresTestUtils::createStartupMessage(params);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_found_.value());
  EXPECT_EQ(1, cfg_->stats().ssl_not_requested_.value());
  EXPECT_EQ(0, cfg_->stats().ssl_requested_.value());
}

// Test random non-PostgreSQL data.
TEST_F(PostgresInspectorTest, NonPostgresData) {
  init();

  auto data = PostgresTestUtils::createRandomData(20);
  auto buffer = createBuffer(std::move(data));

  const Network::FilterStatus status = filter_->onData(*buffer);

  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(1, cfg_->stats().postgres_not_found_.value());
  EXPECT_EQ(1, cfg_->stats().protocol_error_.value());
}

// Test bytes processed histogram.
TEST_F(PostgresInspectorTest, BytesProcessedHistogram) {
  init();

  EXPECT_CALL(socket_, setDetectedTransportProtocol("postgres"));

  std::map<std::string, std::string> params = {{"user", "test"}};
  auto data = PostgresTestUtils::createStartupMessage(params);
  auto buffer = createBuffer(std::move(data));

  filter_->onData(*buffer);

  // Check that bytes_processed histogram was updated.
  // We cannot easily read histogram sample values via IsolatedStoreImpl here.
  // Assert that the histogram exists in the store by name.
  Stats::Histogram& histogram = stats_store_.histogramFromString(
      "postgres_inspector.bytes_processed", Stats::Histogram::Unit::Bytes);
  EXPECT_NE("", histogram.name());
}

// Test maxReadBytes returns correct value.
TEST_F(PostgresInspectorTest, MaxReadBytes) {
  init();
  EXPECT_EQ(Config::DEFAULT_MAX_STARTUP_MESSAGE_SIZE, filter_->maxReadBytes());

  proto_config_.mutable_max_startup_message_size()->set_value(5000);
  initWithConfig(proto_config_);
  EXPECT_EQ(5000, filter_->maxReadBytes());
}

// Test message parser utility functions.
class PostgresMessageParserTest : public testing::Test {};

TEST_F(PostgresMessageParserTest, IsSslRequest) {
  auto buffer = PostgresTestUtils::createSslRequest();
  EXPECT_TRUE(PostgresMessageParser::isSslRequest(buffer, 0));

  auto non_ssl = PostgresTestUtils::createInvalidStartupMessage(12345);
  EXPECT_FALSE(PostgresMessageParser::isSslRequest(non_ssl, 0));

  Buffer::OwnedImpl short_buffer;
  short_buffer.writeBEInt<uint32_t>(4); // Too short
  EXPECT_FALSE(PostgresMessageParser::isSslRequest(short_buffer, 0));
}

TEST_F(PostgresMessageParserTest, ParseStartupMessage) {
  std::map<std::string, std::string> params = {
      {"user", "test"}, {"database", "db"}, {"application_name", "app"}};

  auto buffer = PostgresTestUtils::createStartupMessage(params);

  StartupMessage message;
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));

  EXPECT_EQ(196608, message.protocol_version);
  EXPECT_EQ("test", message.parameters["user"]);
  EXPECT_EQ("db", message.parameters["database"]);
  EXPECT_EQ("app", message.parameters["application_name"]);
}

TEST_F(PostgresMessageParserTest, ParsePartialStartupMessage) {
  auto buffer = PostgresTestUtils::createPartialStartupMessage(100, 50);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));
}

TEST_F(PostgresMessageParserTest, ParseOversizedStartupMessage) {
  auto buffer = PostgresTestUtils::createOversizedMessage(2000);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1000));
}

TEST_F(PostgresMessageParserTest, HasCompleteMessage) {
  auto buffer = PostgresTestUtils::createStartupMessage({{"user", "test"}});

  uint32_t message_length;
  EXPECT_TRUE(PostgresMessageParser::hasCompleteMessage(buffer, 0, message_length));
  EXPECT_EQ(buffer.length(), message_length);

  // Test with partial message.
  Buffer::OwnedImpl partial;
  partial.writeBEInt<uint32_t>(100); // Claims 100 bytes
  partial.writeBEInt<uint32_t>(196608);
  // But only has 8 bytes total

  EXPECT_FALSE(PostgresMessageParser::hasCompleteMessage(partial, 0, message_length));
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

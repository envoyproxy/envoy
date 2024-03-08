#include "envoy/common/time.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/fluentd/config.h"
#include "source/extensions/access_loggers/fluentd/fluentd_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "msgpack.hpp"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {
namespace {

class FluentdAccessLoggerImplTest : public testing::Test {
public:
  FluentdAccessLoggerImplTest()
      : async_client_(new Tcp::AsyncClient::MockAsyncTcpClient()),
        timer_(new Event::MockTimer(&dispatcher_)) {}

  void init(int buffer_size_bytes = 0) {
    EXPECT_CALL(*async_client_, setAsyncTcpClientCallbacks(_));
    EXPECT_CALL(*timer_, enableTimer(_, _));

    config_.set_tag(tag_);
    config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
    logger_ = std::make_unique<FluentdAccessLoggerImpl>(
        Tcp::AsyncTcpClientPtr{async_client_}, dispatcher_, config_, *stats_store_.rootScope());
  }

  std::string getExpectedMsgpackPayload(int entries_count) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer(buffer);
    packer.pack_array(2);
    packer.pack(tag_);
    packer.pack_array(entries_count);
    for (int idx = 0; idx < entries_count; idx++) {
      packer.pack_array(2);
      packer.pack(time_);
      const char* record_bytes = reinterpret_cast<const char*>(&data_[0]);
      packer.pack_bin_body(record_bytes, data_.size());
    }

    return std::string(buffer.data(), buffer.size());
  }

  std::string tag_ = "test.tag";
  uint64_t time_ = 123;
  std::vector<uint8_t> data_ = {10, 20};
  Tcp::AsyncClient::MockAsyncTcpClient* async_client_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<FluentdAccessLoggerImpl> logger_;
  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config_;
};

TEST_F(FluentdAccessLoggerImplTest, NoWriteOnLogIfNotConnectedToUpstream) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, NoWriteOnLogIfBufferLimitNotPassed) {
  init(100);
  EXPECT_CALL(*async_client_, connect()).Times(0);
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, NoWriteOnLogIfDisconnectedByRemote) {
  init();
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    logger_->onEvent(Network::ConnectionEvent::RemoteClose);
    return true;
  }));

  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, NoWriteOnLogIfDisconnectedByLocal) {
  init();
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    logger_->onEvent(Network::ConnectionEvent::LocalClose);
    return true;
  }));

  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, LogSingleEntry) {
  init(); // Default buffer limit is 0 so single entry should be flushed immediately.
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    logger_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, LogTwoEntries) {
  init(12); // First entry is 10 bytes, so first entry should not cause the logger to flush.

  // First log should not be flushed.
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));

  // Expect second entry to cause all entries to flush.
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    logger_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(2);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));
  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
}

TEST_F(FluentdAccessLoggerImplTest, CallbacksTest) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  logger_->log(std::make_unique<Entry>(time_, std::move(data_)));
  EXPECT_NO_THROW(logger_->onAboveWriteBufferHighWatermark());
  EXPECT_NO_THROW(logger_->onBelowWriteBufferLowWatermark());
  Buffer::OwnedImpl buffer;
  EXPECT_NO_THROW(logger_->onData(buffer, false));
}

class FluentdAccessLoggerCacheImplTest : public testing::Test {
public:
  FluentdAccessLoggerCacheImplTest() : logger_cache_(cluster_manager_, scope_, tls_) {}

  void init(bool second_logger = false) {
    async_client1_ = new Tcp::AsyncClient::MockAsyncTcpClient();
    EXPECT_CALL(*async_client1_, setAsyncTcpClientCallbacks(_));

    if (second_logger) {
      async_client2_ = new Tcp::AsyncClient::MockAsyncTcpClient();
      EXPECT_CALL(*async_client2_, setAsyncTcpClientCallbacks(_));
    }
  }

  std::string cluster_name_ = "test_cluster";
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Tcp::AsyncClient::MockAsyncTcpClient* async_client1_;
  Tcp::AsyncClient::MockAsyncTcpClient* async_client2_;
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  FluentdAccessLoggerCacheImpl logger_cache_;
};

TEST_F(FluentdAccessLoggerCacheImplTest, CreateNonExistingLogger) {
  init();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(cluster_name_)).WillOnce(Return(&cluster_));
  EXPECT_CALL(cluster_, tcpAsyncClient(_, _)).WillOnce(Invoke([&] {
    return Tcp::AsyncTcpClientPtr{async_client1_};
  }));

  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config;
  config.set_cluster(cluster_name_);
  config.set_tag("test.tag");
  config.mutable_buffer_size_bytes()->set_value(123);
  auto logger = logger_cache_.getOrCreateLogger(std::make_shared<FluentdAccessLogConfig>(config));
  EXPECT_TRUE(logger != nullptr);
}

TEST_F(FluentdAccessLoggerCacheImplTest, CreateTwoLoggersSameHash) {
  init();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(cluster_name_)).WillOnce(Return(&cluster_));
  EXPECT_CALL(cluster_, tcpAsyncClient(_, _)).WillRepeatedly(Invoke([&] {
    return Tcp::AsyncTcpClientPtr{async_client1_};
  }));

  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config1;
  config1.set_cluster(cluster_name_);
  config1.set_tag("test.tag");
  config1.mutable_buffer_size_bytes()->set_value(123);
  auto logger1 = logger_cache_.getOrCreateLogger(std::make_shared<FluentdAccessLogConfig>(config1));
  EXPECT_TRUE(logger1 != nullptr);

  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config2;
  config2.set_cluster(cluster_name_); // config hash will be different than config1
  config2.set_tag("test.tag");
  config2.mutable_buffer_size_bytes()->set_value(123);
  auto logger2 = logger_cache_.getOrCreateLogger(std::make_shared<FluentdAccessLogConfig>(config2));
  EXPECT_TRUE(logger2 != nullptr);

  // Make sure we got the same logger
  EXPECT_EQ(logger1, logger2);
}

TEST_F(FluentdAccessLoggerCacheImplTest, CreateTwoLoggersDifferentHash) {
  init(true);
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(&cluster_))
      .WillOnce(Return(&cluster_));

  EXPECT_CALL(cluster_, tcpAsyncClient(_, _))
      .WillOnce(Invoke([&] { return Tcp::AsyncTcpClientPtr{async_client1_}; }))
      .WillOnce(Invoke([&] { return Tcp::AsyncTcpClientPtr{async_client2_}; }));

  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config1;
  config1.set_cluster(cluster_name_);
  config1.set_tag("test.tag");
  config1.mutable_buffer_size_bytes()->set_value(123);
  auto logger1 = logger_cache_.getOrCreateLogger(std::make_shared<FluentdAccessLogConfig>(config1));
  EXPECT_TRUE(logger1 != nullptr);

  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config2;
  config2.set_cluster("different_cluster"); // config hash will be different than config1
  config2.set_tag("test.tag");
  config2.mutable_buffer_size_bytes()->set_value(123);
  auto logger2 = logger_cache_.getOrCreateLogger(std::make_shared<FluentdAccessLogConfig>(config2));
  EXPECT_TRUE(logger2 != nullptr);

  // Make sure we got two different loggers
  EXPECT_NE(logger1, logger2);
}

class MockFluentdAccessLogger : public FluentdAccessLogger {
public:
  MOCK_METHOD(void, log, (EntryPtr &&));
};

class MockFluentdAccessLoggerCache : public FluentdAccessLoggerCache {
public:
  MOCK_METHOD(FluentdAccessLoggerSharedPtr, getOrCreateLogger,
              (const FluentdAccessLogConfigSharedPtr));
};

class MockFluentdFormatter : public FluentdFormatter {
public:
  MOCK_METHOD(std::vector<uint8_t>, format,
              (const Formatter::HttpFormatterContext& context,
               const StreamInfo::StreamInfo& stream_info),
              (const));
};

using FilterPtr = Envoy::AccessLog::FilterPtr;

class FluentdAccessLogTest : public testing::Test {
public:
  FluentdAccessLogTest() {
    ON_CALL(*filter_, evaluate(_, _)).WillByDefault(Return(true));
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_)).WillOnce(Return(logger_));
  }

  AccessLog::MockFilter* filter_{new NiceMock<AccessLog::MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig config_;
  MockFluentdFormatter* formatter_{new NiceMock<MockFluentdFormatter>()};
  std::shared_ptr<MockFluentdAccessLogger> logger_{new MockFluentdAccessLogger()};
  std::shared_ptr<MockFluentdAccessLoggerCache> logger_cache_{new MockFluentdAccessLoggerCache()};
};

TEST_F(FluentdAccessLogTest, CreateAndLog) {
  auto access_log =
      FluentdAccessLog(AccessLog::FilterPtr{filter_}, FluentdFormatterPtr{formatter_},
                       std::make_shared<FluentdAccessLogConfig>(config_), tls_, logger_cache_);

  MockTimeSystem time_system;
  EXPECT_CALL(time_system, systemTime).WillOnce(Return(SystemTime(std::chrono::seconds(200))));
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, timeSource()).WillOnce(ReturnRef(time_system));

  EXPECT_CALL(*formatter_, format(_, _)).WillOnce(Return(std::vector<uint8_t>{10, 20}));
  EXPECT_CALL(*logger_, log(_)).WillOnce(Invoke([](EntryPtr&& entry) {
    EXPECT_EQ(200, entry->time_);
    ASSERT_EQ(2, entry->record_.size());
    EXPECT_EQ(uint8_t(10), entry->record_[0]);
    EXPECT_EQ(uint8_t(20), entry->record_[1]);
  }));

  access_log.log({}, stream_info);
}

} // namespace
} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

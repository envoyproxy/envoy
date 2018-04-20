#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/filter/fault/v2/fault.pb.h"

#include "common/stats/stats_impl.h"

#include "extensions/filters/network/mongo_proxy/bson_impl.h"
#include "extensions/filters/network/mongo_proxy/codec_impl.h"
#include "extensions/filters/network/mongo_proxy/proxy.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class MockDecoder : public Decoder {
public:
  MOCK_METHOD1(onData, void(Buffer::Instance& data));
};

class TestStatStore : public Stats::IsolatedStoreImpl {
public:
  MOCK_METHOD2(deliverHistogramToSinks, void(const Stats::Histogram& histogram, uint64_t value));
};

class TestProxyFilter : public ProxyFilter {
public:
  using ProxyFilter::ProxyFilter;

  // ProxyFilter
  DecoderPtr createDecoder(DecoderCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }

  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* callbacks_{};
};

class MongoProxyFilterTest : public testing::Test {
public:
  MongoProxyFilterTest() { setup(); }

  void setup() {
    ON_CALL(runtime_.snapshot_, featureEnabled("mongo.proxy_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("mongo.connection_logging_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("mongo.logging_enabled", 100))
        .WillByDefault(Return(true));

    EXPECT_CALL(log_manager_, createAccessLog(_)).WillOnce(Return(file_));
    access_log_.reset(new AccessLog("test", log_manager_));
  }

  void initializeFilter() {
    filter_.reset(new TestProxyFilter("test.", store_, runtime_, access_log_, fault_config_,
                                      drain_decision_));
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void setupDelayFault(bool enable_fault) {
    envoy::config::filter::fault::v2::FaultDelay fault{};
    fault.set_percent(50);
    fault.mutable_fixed_delay()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(10));

    fault_config_.reset(new FaultConfig(fault));

    EXPECT_CALL(runtime_.snapshot_, featureEnabled(_, _)).Times(AnyNumber());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("mongo.fault.fixed_delay.percent", 50))
        .WillOnce(Return(enable_fault));

    if (enable_fault) {
      EXPECT_CALL(runtime_.snapshot_, getInteger("mongo.fault.fixed_delay.duration_ms", 10))
          .WillOnce(Return(10));
    }
  }

  Buffer::OwnedImpl fake_data_;
  NiceMock<TestStatStore> store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Filesystem::MockFile> file_{new NiceMock<Filesystem::MockFile>()};
  AccessLogSharedPtr access_log_;
  FaultConfigSharedPtr fault_config_;
  std::unique_ptr<TestProxyFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  Envoy::AccessLog::MockAccessLogManager log_manager_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
};

TEST_F(MongoProxyFilterTest, DelayFaults) {
  setupDelayFault(true);
  initializeFilter();

  Event::MockTimer* delay_timer =
      new Event::MockTimer(&read_filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*delay_timer, enableTimer(std::chrono::milliseconds(10)));
  EXPECT_CALL(*file_, write(_)).Times(AtLeast(1));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));
  }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data_, false));
  EXPECT_EQ(1U, store_.counter("test.op_query").value());

  // Requests during active delay.
  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));
  }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data_, false));
  EXPECT_EQ(2U, store_.counter("test.op_query").value());

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    GetMoreMessagePtr message(new GetMoreMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->cursorId(1);
    filter_->callbacks_->decodeGetMore(std::move(message));
  }));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data_, false));
  EXPECT_EQ(1U, store_.counter("test.op_get_more").value());

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    KillCursorsMessagePtr message(new KillCursorsMessageImpl(0, 0));
    message->numberOfCursorIds(1);
    message->cursorIds({1});
    filter_->callbacks_->decodeKillCursors(std::move(message));
  }));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data_, false));
  EXPECT_EQ(1U, store_.counter("test.op_kill_cursors").value());

  EXPECT_CALL(read_filter_callbacks_, continueReading());
  delay_timer->callback_();
  EXPECT_EQ(1U, store_.counter("test.delays_injected").value());
}

TEST_F(MongoProxyFilterTest, DelayFaultsRuntimeDisabled) {
  setupDelayFault(false);
  initializeFilter();

  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*file_, write(_)).Times(AtLeast(1));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));
  }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data_, false));
  EXPECT_EQ(0U, store_.counter("test.delays_injected").value());
}

TEST_F(MongoProxyFilterTest, Stats) {
  initializeFilter();

  EXPECT_CALL(*file_, write(_)).Times(AtLeast(1));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_num_docs"), 1));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_size"), 22));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_time_ms"), _));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    ReplyMessagePtr message(new ReplyMessageImpl(0, 0));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    filter_->callbacks_->decodeReply(std::move(message));

  }));
  filter_->onWrite(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.op_query").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_tailable_cursor").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_no_cursor_timeout").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_await_data").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_exhaust").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_no_max_time").value());
  EXPECT_EQ(1U, store_.counter("test.op_query_scatter_get").value());

  EXPECT_EQ(1U, store_.counter("test.collection.test.query.total").value());
  EXPECT_EQ(1U, store_.counter("test.collection.test.query.scatter_get").value());

  EXPECT_EQ(1U, store_.counter("test.op_reply").value());
  EXPECT_EQ(1U, store_.counter("test.op_reply_cursor_not_found").value());
  EXPECT_EQ(1U, store_.counter("test.op_reply_query_failure").value());
  EXPECT_EQ(1U, store_.counter("test.op_reply_valid_cursor").value());

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    GetMoreMessagePtr message(new GetMoreMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->cursorId(1);
    filter_->callbacks_->decodeGetMore(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InsertMessagePtr message(new InsertMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->documents().push_back(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeInsert(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    KillCursorsMessagePtr message(new KillCursorsMessageImpl(0, 0));
    message->numberOfCursorIds(1);
    message->cursorIds({1});
    filter_->callbacks_->decodeKillCursors(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    CommandMessagePtr message(new CommandMessageImpl(0, 0));
    message->database(std::string("Test database"));
    message->commandName(std::string("Test command name"));
    message->metadata(Bson::DocumentImpl::create());
    message->commandArgs(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeCommand(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    CommandReplyMessagePtr message(new CommandReplyMessageImpl(0, 0));
    message->metadata(Bson::DocumentImpl::create());
    message->commandReply(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeCommandReply(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.op_get_more").value());
  EXPECT_EQ(1U, store_.counter("test.op_insert").value());
  EXPECT_EQ(1U, store_.counter("test.op_kill_cursors").value());
  EXPECT_EQ(0U, store_.counter("test.delays_injected").value());
  EXPECT_EQ(1U, store_.counter("test.op_command").value());
  EXPECT_EQ(1U, store_.counter("test.op_command_reply").value());
}

TEST_F(MongoProxyFilterTest, CommandStats) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.$cmd");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "test.cmd.foo.reply_num_docs"), 1));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "test.cmd.foo.reply_size"), 22));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "test.cmd.foo.reply_time_ms"), _));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    ReplyMessagePtr message(new ReplyMessageImpl(0, 0));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    filter_->callbacks_->decodeReply(std::move(message));
  }));
  filter_->onWrite(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.cmd.foo.total").value());
}

TEST_F(MongoProxyFilterTest, CallingFunctionStats) {
  initializeFilter();

  std::string json = R"EOF(
    {
      "hostname":"api-production-iad-canary",
      "httpUniqueId":"VqqX7H8AAQEAAE@8EUkAAAAR",
      "callingFunction":"getByMongoId"
    }
  )EOF";

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addString("$comment", std::move(json)));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.collection.test.query.total").value());
  EXPECT_EQ(1U, store_.counter("test.collection.test.query.scatter_get").value());
  EXPECT_EQ(1U, store_.counter("test.collection.test.callsite.getByMongoId.query.total").value());
  EXPECT_EQ(1U,
            store_.counter("test.collection.test.callsite.getByMongoId.query.scatter_get").value());

  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_num_docs"), 1));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_size"), 22));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "test.collection.test.query.reply_time_ms"), _));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name,
                           "test.collection.test.callsite.getByMongoId.query.reply_num_docs"),
                  1));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "test.collection.test.callsite.getByMongoId.query.reply_size"),
                          22));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name,
                           "test.collection.test.callsite.getByMongoId.query.reply_time_ms"),
                  _));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    ReplyMessagePtr message(new ReplyMessageImpl(0, 0));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    filter_->callbacks_->decodeReply(std::move(message));

  }));
  filter_->onWrite(fake_data_, false);
}

TEST_F(MongoProxyFilterTest, MultiGet) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addDocument(
        "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create())));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.op_query_multi_get").value());
  EXPECT_EQ(1U, store_.counter("test.collection.test.query.multi_get").value());
}

TEST_F(MongoProxyFilterTest, MaxTime) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addInt32("$maxTimeMS", 100));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_EQ(0U, store_.counter("test.op_query_no_max_time").value());
}

TEST_F(MongoProxyFilterTest, DecodeError) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw EnvoyException("bad decode");
  }));
  filter_->onData(fake_data_, false);

  // Should not call decode again.
  filter_->onData(fake_data_, false);

  EXPECT_EQ(1U, store_.counter("test.decoding_error").value());
}

TEST_F(MongoProxyFilterTest, ConcurrentQueryWithDrainClose) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(1, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));

    message.reset(new QueryMessageImpl(2, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create());
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);
  EXPECT_EQ(2U, store_.gauge("test.op_query_active").value());

  Event::MockTimer* drain_timer = nullptr;
  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    ReplyMessagePtr message(new ReplyMessageImpl(0, 1));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    filter_->callbacks_->decodeReply(std::move(message));

    message.reset(new ReplyMessageImpl(0, 2));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    ON_CALL(runtime_.snapshot_, featureEnabled("mongo.drain_close_enabled", 100))
        .WillByDefault(Return(true));
    EXPECT_CALL(drain_decision_, drainClose()).WillOnce(Return(true));
    drain_timer = new Event::MockTimer(&read_filter_callbacks_.connection_.dispatcher_);
    EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(0)));
    filter_->callbacks_->decodeReply(std::move(message));
  }));
  filter_->onWrite(fake_data_, false);

  EXPECT_CALL(read_filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->callback_();

  EXPECT_EQ(0U, store_.gauge("test.op_query_active").value());
  EXPECT_EQ(1U, store_.counter("test.cx_drain_close").value());
}

TEST_F(MongoProxyFilterTest, EmptyActiveQueryList) {
  initializeFilter();

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.$cmd");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addString("foo", "bar"));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    ReplyMessagePtr message(new ReplyMessageImpl(0, 0));
    message->flags(0b11);
    message->cursorId(1);
    message->documents().push_back(Bson::DocumentImpl::create()->addString("hello", "world"));
    filter_->callbacks_->decodeReply(std::move(message));
  }));
  filter_->onWrite(fake_data_, false);
  read_filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(MongoProxyFilterTest, ConnectionDestroyLocal) {
  setupDelayFault(true);
  initializeFilter();

  Event::MockTimer* delay_timer =
      new Event::MockTimer(&read_filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*delay_timer, enableTimer(std::chrono::milliseconds(10)));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addDocument(
        "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create())));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*delay_timer, disableTimer());
  read_filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(MongoProxyFilterTest, ConnectionDestroyRemote) {
  setupDelayFault(true);
  initializeFilter();

  Event::MockTimer* delay_timer =
      new Event::MockTimer(&read_filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*delay_timer, enableTimer(std::chrono::milliseconds(10)));

  EXPECT_CALL(*filter_->decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    QueryMessagePtr message(new QueryMessageImpl(0, 0));
    message->fullCollectionName("db.test");
    message->flags(0b1110010);
    message->query(Bson::DocumentImpl::create()->addDocument(
        "_id", Bson::DocumentImpl::create()->addArray("$in", Bson::DocumentImpl::create())));
    filter_->callbacks_->decodeQuery(std::move(message));
  }));
  filter_->onData(fake_data_, false);

  EXPECT_CALL(*delay_timer, disableTimer());
  read_filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

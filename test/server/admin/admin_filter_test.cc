#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/admin_filter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Server {

class AdminFilterTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminFilterTest() : filter_(admin_), request_headers_{{":path", "/"}} {
    ON_CALL(admin_, makeRequest(_)).WillByDefault([](AdminStream&) {
      return adminHandlerCallback();
    });
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockAdmin> admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;

  static Admin::RequestPtr adminHandlerCallback() {
    return AdminImpl::makeStaticTextRequest("OK\n", Http::Code::OK);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminFilterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, true));
  ASSERT_TRUE(callbacks_.stream_info_.responseCodeDetails().has_value());
  EXPECT_EQ(callbacks_.stream_info_.responseCodeDetails().value(), "admin_filter_response");
}

TEST_P(AdminFilterTest, Body) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, true));
}

TEST_P(AdminFilterTest, Trailers) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, false));
  EXPECT_CALL(callbacks_, decodingBuffer());
  filter_.getRequestBody();
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_.decodeTrailers(request_trailers));
}

// Emits chunks "0", "1", ... up to num_chunks.
class CountingRequest : public Admin::Request {
public:
  CountingRequest(uint32_t num_chunks, bool& destroyed)
      : num_chunks_(num_chunks), destroyed_(destroyed) {}
  ~CountingRequest() override { destroyed_ = true; }

  Http::Code start(Http::ResponseHeaderMap&) override { return Http::Code::OK; }
  bool nextChunk(Buffer::Instance& response) override {
    response.add(absl::StrCat(next_));
    return ++next_ < num_chunks_;
  }

private:
  const uint32_t num_chunks_;
  bool& destroyed_;
  uint32_t next_{0};
};

class AdminFilterChunkedTest : public AdminFilterTest {
public:
  void expectCountingRequest(uint32_t num_chunks) {
    EXPECT_CALL(admin_, makeRequest(_))
        .WillOnce([this, num_chunks](AdminStream&) -> Admin::RequestPtr {
          return std::make_unique<CountingRequest>(num_chunks, request_destroyed_);
        });
  }

  void expectData(absl::string_view data, bool end_stream) {
    EXPECT_CALL(callbacks_, encodeData(BufferString(std::string(data)), end_stream));
  }

  // The guard is latched when the filter is created.
  AdminFilter& createYieldingFilter() {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.admin_filter_yield_between_chunks", "true"}});
    yielding_filter_ = std::make_unique<AdminFilter>(admin_);
    yielding_filter_->setDecoderFilterCallbacks(callbacks_);
    return *yielding_filter_;
  }

  TestScopedRuntime scoped_runtime_;
  std::unique_ptr<AdminFilter> yielding_filter_;
  bool request_destroyed_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminFilterChunkedTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminFilterChunkedTest, YieldsToEventLoopBetweenChunks) {
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  expectData("0", false);
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(next_chunk_cb->enabled_);

  expectData("1", false);
  next_chunk_cb->invokeCallback();
  EXPECT_TRUE(next_chunk_cb->enabled_);

  expectData("2", true);
  next_chunk_cb->invokeCallback();
  EXPECT_FALSE(next_chunk_cb->enabled_);
  EXPECT_TRUE(request_destroyed_);
  filter.onDestroy();
}

TEST_P(AdminFilterChunkedTest, DestroyCancelsRemainingChunks) {
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  testing::MockFunction<void()> cb_destroyed;
  new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_, &cb_destroyed);

  expectData("0", false);
  filter.decodeHeaders(request_headers_, true);
  EXPECT_FALSE(request_destroyed_);

  EXPECT_CALL(cb_destroyed, Call());
  filter.onDestroy();
  EXPECT_TRUE(request_destroyed_);
}

TEST_P(AdminFilterChunkedTest, ResetWhileEncodingStopsChunks) {
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  EXPECT_CALL(callbacks_.dispatcher_, createSchedulableCallback_(_)).Times(0);
  // The stream is reset from under encodeData().
  EXPECT_CALL(callbacks_, encodeData(_, false)).WillOnce([&filter](Buffer::Instance&, bool) {
    filter.onDestroy();
  });
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(request_destroyed_);
}

TEST_P(AdminFilterChunkedTest, AllChunksAtOnceByDefault) {
  expectCountingRequest(3);
  EXPECT_CALL(callbacks_.dispatcher_, createSchedulableCallback_(_)).Times(0);

  InSequence s;
  expectData("0", false);
  expectData("1", false);
  expectData("2", true);
  filter_.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(request_destroyed_);
  filter_.onDestroy();
}

TEST_P(AdminFilterChunkedTest, ResetWhileEncodingStopsChunksByDefault) {
  expectCountingRequest(3);
  // The stream is reset from under encodeData(), so the remaining chunks are dropped.
  EXPECT_CALL(callbacks_, encodeData(_, false)).WillOnce([this](Buffer::Instance&, bool) {
    filter_.onDestroy();
  });
  filter_.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(request_destroyed_);
}

class AdminFilterFlowControlTest : public AdminFilterChunkedTest {
public:
  void setBufferLimit(uint32_t limit) {
    ON_CALL(connection_, bufferLimit()).WillByDefault(Return(limit));
    ON_CALL(callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  }

  NiceMock<Network::MockConnection> connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminFilterFlowControlTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminFilterFlowControlTest, PausesOnHighWatermarkAndResumesOnLowWatermark) {
  setBufferLimit(1024);
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  // The first chunk fills the downstream write buffer.
  EXPECT_CALL(callbacks_, encodeData(BufferString("0"), false))
      .WillOnce([&filter](Buffer::Instance&, bool) { filter.onAboveWriteBufferHighWatermark(); });
  filter.decodeHeaders(request_headers_, true);
  EXPECT_FALSE(next_chunk_cb->enabled_);
  EXPECT_FALSE(request_destroyed_);

  filter.onBelowWriteBufferLowWatermark();
  EXPECT_TRUE(next_chunk_cb->enabled_);
  expectData("1", false);
  next_chunk_cb->invokeCallback();
  // Back to one chunk per event loop iteration.
  EXPECT_TRUE(next_chunk_cb->enabled_);
  expectData("2", true);
  next_chunk_cb->invokeCallback();
  EXPECT_TRUE(request_destroyed_);
  filter.onDestroy();
}

TEST_P(AdminFilterFlowControlTest, NoFlowControlWithoutYielding) {
  setBufferLimit(1024);
  EXPECT_CALL(callbacks_, addDownstreamWatermarkCallbacks(_)).Times(0);
  AdminFilter filter(admin_);
  filter.setDecoderFilterCallbacks(callbacks_);
  expectCountingRequest(3);
  EXPECT_CALL(callbacks_.dispatcher_, createSchedulableCallback_(_)).Times(0);

  InSequence s;
  expectData("0", false);
  expectData("1", false);
  expectData("2", true);
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(request_destroyed_);
  EXPECT_CALL(callbacks_, removeDownstreamWatermarkCallbacks(_)).Times(0);
  filter.onDestroy();
}

TEST_P(AdminFilterFlowControlTest, HighWatermarkWhileScheduledStopsNextChunk) {
  setBufferLimit(1024);
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  expectData("0", false);
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(next_chunk_cb->enabled_);

  // The downstream backs up before the scheduled chunk runs.
  filter.onAboveWriteBufferHighWatermark();
  EXPECT_CALL(callbacks_, encodeData(_, _)).Times(0);
  next_chunk_cb->invokeCallback();
  EXPECT_FALSE(next_chunk_cb->enabled_);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  filter.onBelowWriteBufferLowWatermark();
  expectData("1", false);
  next_chunk_cb->invokeCallback();
  filter.onDestroy();
  EXPECT_TRUE(request_destroyed_);
}

TEST_P(AdminFilterFlowControlTest, WaitsForEveryLowWatermark) {
  setBufferLimit(1024);
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(2);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  // Both the stream and the connection go above their high watermark.
  EXPECT_CALL(callbacks_, encodeData(BufferString("0"), false))
      .WillOnce([&filter](Buffer::Instance&, bool) {
        filter.onAboveWriteBufferHighWatermark();
        filter.onAboveWriteBufferHighWatermark();
      });
  filter.decodeHeaders(request_headers_, true);

  filter.onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(next_chunk_cb->enabled_);
  filter.onBelowWriteBufferLowWatermark();
  EXPECT_TRUE(next_chunk_cb->enabled_);

  expectData("1", true);
  next_chunk_cb->invokeCallback();
  EXPECT_TRUE(request_destroyed_);
  filter.onDestroy();
}

TEST_P(AdminFilterFlowControlTest, StartsPausedWhenAlreadyAboveHighWatermark) {
  setBufferLimit(1024);
  // Registration replays a high watermark the downstream already raised.
  EXPECT_CALL(callbacks_, addDownstreamWatermarkCallbacks(_))
      .WillOnce([](Http::DownstreamWatermarkCallbacks& callbacks) {
        callbacks.onAboveWriteBufferHighWatermark();
      });
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(2);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks_, encodeData(_, _)).Times(0);
  filter.decodeHeaders(request_headers_, true);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  filter.onBelowWriteBufferLowWatermark();
  EXPECT_TRUE(next_chunk_cb->enabled_);
  expectData("0", false);
  next_chunk_cb->invokeCallback();
  expectData("1", true);
  next_chunk_cb->invokeCallback();
  EXPECT_TRUE(request_destroyed_);
  filter.onDestroy();
}

TEST_P(AdminFilterFlowControlTest, DestroyWhilePausedReleasesRequest) {
  setBufferLimit(1024);
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(3);
  EXPECT_CALL(callbacks_, encodeData(BufferString("0"), false))
      .WillOnce([&filter](Buffer::Instance&, bool) { filter.onAboveWriteBufferHighWatermark(); });
  filter.decodeHeaders(request_headers_, true);
  EXPECT_FALSE(request_destroyed_);

  EXPECT_CALL(callbacks_, removeDownstreamWatermarkCallbacks(_));
  filter.onDestroy();
  EXPECT_TRUE(request_destroyed_);
}

TEST_P(AdminFilterFlowControlTest, NoBufferLimitSkipsFlowControl) {
  setBufferLimit(0);
  EXPECT_CALL(callbacks_, addDownstreamWatermarkCallbacks(_)).Times(0);
  AdminFilter& filter = createYieldingFilter();
  expectCountingRequest(2);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  expectData("0", false);
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(next_chunk_cb->enabled_);
  expectData("1", true);
  next_chunk_cb->invokeCallback();
  EXPECT_TRUE(request_destroyed_);
  EXPECT_CALL(callbacks_, removeDownstreamWatermarkCallbacks(_)).Times(0);
  filter.onDestroy();
}

TEST_P(AdminFilterFlowControlTest, LowWatermarkDuringLastChunkDoesNotEncodeAgain) {
  setBufferLimit(1024);
  AdminFilter& filter = createYieldingFilter();
  // The stream stays open after the last chunk, as with tap or hystrix.
  filter.setEndStreamOnComplete(false);
  expectCountingRequest(1);
  auto* next_chunk_cb = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);

  // The last chunk goes above and back below inline, which schedules a callback.
  EXPECT_CALL(callbacks_, encodeData(BufferString("0"), false))
      .WillOnce([&filter](Buffer::Instance&, bool) {
        filter.onAboveWriteBufferHighWatermark();
        filter.onBelowWriteBufferLowWatermark();
      });
  filter.decodeHeaders(request_headers_, true);
  EXPECT_TRUE(request_destroyed_);
  EXPECT_TRUE(next_chunk_cb->enabled_);

  EXPECT_CALL(callbacks_, encodeData(_, _)).Times(0);
  next_chunk_cb->invokeCallback();
  filter.onDestroy();
}

} // namespace Server
} // namespace Envoy

#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

#include "test/common/buffer/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/ws_local_ratelimit/filters/http/source/ws_local_ratelimit_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {
namespace {

// Encodes a single unmasked text frame carrying `payload`, in the same shape a downstream
// client's frames arrive in decodeData() once unmasked (masking is irrelevant to the decoder).
Buffer::OwnedImpl encodeTextFrame(absl::string_view payload) {
  WebSocket::Encoder encoder;
  WebSocket::Frame frame;
  frame.final_fragment_ = true;
  frame.opcode_ = WebSocket::kFrameOpcodeText;
  frame.masking_key_ = std::nullopt;
  frame.payload_length_ = payload.size();
  frame.payload_ = std::make_unique<Buffer::OwnedImpl>(payload.data(), payload.size());

  Buffer::OwnedImpl out;
  auto header = encoder.encodeFrameHeader(frame);
  out.add(header->data(), header->size());
  out.add(*frame.payload_);
  return out;
}

envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit
makeConfig(uint32_t max_tokens, absl::string_view rejection_message = "") {
  envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit config;
  config.set_stat_prefix("test");
  config.mutable_token_bucket()->set_max_tokens(max_tokens);
  config.mutable_token_bucket()->mutable_tokens_per_fill()->set_value(max_tokens);
  *config.mutable_token_bucket()->mutable_fill_interval() =
      Protobuf::util::TimeUtil::MillisecondsToDuration(1000000);
  config.set_rejection_message(std::string(rejection_message));
  return config;
}

class WsLocalRateLimitFilterTest : public testing::Test {
public:
  WsLocalRateLimitFilterTest() {
    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(decoder_callbacks_2_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    // By default simulate the common case: the upstream's response (e.g. the WebSocket upgrade
    // response) has already arrived, so sendRejectionFrame() is safe to inject encodeData().
    // ExceedingBudgetBeforeResponseHeadersDropsWithoutCrashing overrides this to test the
    // opposite case.
    ON_CALL(decoder_callbacks_, responseHeaders())
        .WillByDefault(testing::Return(Http::ResponseHeaderMapOptRef(response_headers_)));
    ON_CALL(decoder_callbacks_2_, responseHeaders())
        .WillByDefault(testing::Return(Http::ResponseHeaderMapOptRef(response_headers_)));
  }

  void setup(uint32_t max_tokens, absl::string_view rejection_message = "") {
    config_ = std::make_shared<WsLocalRateLimitConfig>(makeConfig(max_tokens, rejection_message),
                                                       *stats_.rootScope());
    filter_ = std::make_shared<WsLocalRateLimitFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);

    filter_2_ = std::make_shared<WsLocalRateLimitFilter>(config_);
    filter_2_->setDecoderFilterCallbacks(decoder_callbacks_2_);
  }

  Http::TestRequestHeaderMapImpl websocketUpgradeHeaders() {
    return Http::TestRequestHeaderMapImpl{
        {":method", "GET"}, {":path", "/"}, {"connection", "upgrade"}, {"upgrade", "websocket"}};
  }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_2_;
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"}};

  WsLocalRateLimitConfigSharedPtr config_;
  std::shared_ptr<WsLocalRateLimitFilter> filter_;
  std::shared_ptr<WsLocalRateLimitFilter> filter_2_;
};

TEST_F(WsLocalRateLimitFilterTest, NonWebSocketRequestPassesThrough) {
  setup(1);
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl data("plain http body");
  const std::string original = data.toString();
  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, testing::_)).Times(0);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(original, data.toString());
}

TEST_F(WsLocalRateLimitFilterTest, FramesWithinBudgetAreForwarded) {
  setup(5);
  auto headers = websocketUpgradeHeaders();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, testing::_)).Times(0);
  for (int i = 0; i < 5; ++i) {
    Buffer::OwnedImpl frame = encodeTextFrame("hello");
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(frame, false));
    EXPECT_GT(frame.length(), 0);
  }
  EXPECT_EQ(0, TestUtility::findCounter(stats_, "test.ws_local_rate_limit.rate_limited")->value());
  EXPECT_EQ(5, TestUtility::findCounter(stats_, "test.ws_local_rate_limit.ok")->value());
}

TEST_F(WsLocalRateLimitFilterTest, ExceedingBudgetDropsMessageByDefault) {
  // No rejection_message configured: the default is to silently drop the frame, no response
  // sent to the client.
  setup(1);
  auto headers = websocketUpgradeHeaders();
  filter_->decodeHeaders(headers, false);

  Buffer::OwnedImpl allowed_frame = encodeTextFrame("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(allowed_frame, false));

  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, testing::_)).Times(0);
  Buffer::OwnedImpl second_frame = encodeTextFrame("world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(second_frame, false));

  // The rejected frame is dropped, not forwarded upstream.
  EXPECT_EQ(0, second_frame.length());
  EXPECT_EQ(1, TestUtility::findCounter(stats_, "test.ws_local_rate_limit.rate_limited")->value());
}

TEST_F(WsLocalRateLimitFilterTest, ExceedingBudgetSendsConfiguredRejectionMessage) {
  setup(1, "rate limit exceeded");
  auto headers = websocketUpgradeHeaders();
  filter_->decodeHeaders(headers, false);

  Buffer::OwnedImpl allowed_frame = encodeTextFrame("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(allowed_frame, false));

  Buffer::OwnedImpl rejected_reply;
  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, false))
      .WillOnce(testing::Invoke(
          [&rejected_reply](Buffer::Instance& data, bool) { rejected_reply.move(data); }));
  Buffer::OwnedImpl second_frame = encodeTextFrame("world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(second_frame, false));

  // The rejected frame is dropped, not forwarded upstream.
  EXPECT_EQ(0, second_frame.length());
  EXPECT_NE(std::string::npos, rejected_reply.toString().find("rate limit exceeded"));
  EXPECT_EQ(1, TestUtility::findCounter(stats_, "test.ws_local_rate_limit.rate_limited")->value());
}

// Regression coverage for a real crash: calling decoder_callbacks_->encodeData() before any
// response headers exist for the stream (e.g. a client bursts frames fast enough to exceed the
// budget before the upstream's WebSocket upgrade response has arrived) violates FilterManager's
// encode-iteration invariants and aborts the process (ASSERT(headers_continued_) in
// commonHandleAfterDataCallback). The frame must still be dropped; only the client notification
// is skipped.
TEST_F(WsLocalRateLimitFilterTest, ExceedingBudgetBeforeResponseHeadersDropsWithoutCrashing) {
  setup(1, "rate limit exceeded");
  EXPECT_CALL(decoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(Http::ResponseHeaderMapOptRef()));

  auto headers = websocketUpgradeHeaders();
  filter_->decodeHeaders(headers, false);

  Buffer::OwnedImpl allowed_frame = encodeTextFrame("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(allowed_frame, false));

  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, testing::_)).Times(0);
  Buffer::OwnedImpl second_frame = encodeTextFrame("world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(second_frame, false));

  // The rejected frame is still dropped, not forwarded upstream, even though the client
  // couldn't be notified.
  EXPECT_EQ(0, second_frame.length());
  EXPECT_EQ(1, TestUtility::findCounter(stats_, "test.ws_local_rate_limit.rate_limited")->value());
}

TEST_F(WsLocalRateLimitFilterTest, EachConnectionGetsItsOwnBudget) {
  setup(1);
  auto headers = websocketUpgradeHeaders();
  filter_->decodeHeaders(headers, false);
  filter_2_->decodeHeaders(headers, false);

  Buffer::OwnedImpl first_conn_frame = encodeTextFrame("hello");
  EXPECT_CALL(decoder_callbacks_, encodeData(testing::_, testing::_)).Times(0);
  filter_->decodeData(first_conn_frame, false);

  Buffer::OwnedImpl second_conn_frame = encodeTextFrame("hello");
  EXPECT_CALL(decoder_callbacks_2_, encodeData(testing::_, testing::_)).Times(0);
  filter_2_->decodeData(second_conn_frame, false);
}

} // namespace
} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

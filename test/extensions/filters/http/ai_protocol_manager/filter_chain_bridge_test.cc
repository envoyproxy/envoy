#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// Records the resume notifications the bridge delivers once back-pressure drains.
class RecordingHandler : public ReplayResumeHandler {
public:
  void onReplayResumed() override { ++resumed_; }
  int resumed_{0};
};

// DecoderFilterChainBridge maps the path-agnostic bridge surface onto
// StreamDecoderFilterCallbacks and forwards upstream watermarks.
class DecoderFilterChainBridgeTest : public testing::Test {
public:
  // The bridge samples the ingest buffer limit once, at construction, through
  // decoderBufferLimit() -- a non-virtual wrapper over the mock's bufferLimit(). 100 keeps the
  // watermarks small enough for a single addUnacked() to cross them (high=100, low=50).
  DecoderFilterChainBridgeTest() {
    EXPECT_CALL(callbacks_, bufferLimit()).WillOnce(Return(100));
    EXPECT_CALL(callbacks_, addUpstreamWatermarkCallbacks(_))
        .WillOnce(Invoke([this](Http::UpstreamWatermarkCallbacks& cb) { subscriber_ = &cb; }));
    bridge_ = std::make_unique<DecoderFilterChainBridge>(callbacks_, stats_);
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  AiProtocolManagerStats stats_{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), ""))};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::unique_ptr<DecoderFilterChainBridge> bridge_;
  Http::UpstreamWatermarkCallbacks* subscriber_{nullptr};
  RecordingHandler handler_;
};

TEST_F(DecoderFilterChainBridgeTest, DispatcherIsTheFilterCallbacksDispatcher) {
  EXPECT_EQ(&bridge_->dispatcher(), &callbacks_.dispatcher_);
}

TEST_F(DecoderFilterChainBridgeTest, InjectsNonTerminalDecodedData) {
  Buffer::OwnedImpl data("chunk");
  EXPECT_CALL(callbacks_, injectDecodedDataToFilterChain(_, /*end_stream=*/false));
  bridge_->injectData(data);
}

TEST_F(DecoderFilterChainBridgeTest, IngestBackpressureDrivesDecoderWriteBuffer) {
  EXPECT_CALL(callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());

  EXPECT_CALL(callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());
}

// The bridge subscribes for its whole life rather than per handler, so the depth it reports is
// continuous; detaching unsubscribes.
TEST_F(DecoderFilterChainBridgeTest, SubscribesAtConstructionAndUnsubscribesOnDetach) {
  EXPECT_EQ(subscriber_, bridge_.get());
  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();
}

// The drain edge reaches the handler; detaching silences it.
TEST_F(DecoderFilterChainBridgeTest, ResumesTheHandlerWhenWatermarksDrain) {
  bridge_->setReplayHandler(handler_);

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->replayPaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->replayPaused());
  EXPECT_EQ(handler_.resumed_, 1);

  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();

  // The depth keeps tracking, but with no handler nothing is notified.
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.resumed_, 1);
}

// Watermarks delivered before a handler is set are tracked but notify nobody.
TEST_F(DecoderFilterChainBridgeTest, WatermarksWithNoHandlerAreNoOps) {
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->replayPaused());
}

// Pause is a level the bridge holds, not an edge it delivers: a handler registered while the chain
// is already paused sees that state rather than starting a replay into a full buffer, and the
// drain that follows reaches it even though it missed the pause.
TEST_F(DecoderFilterChainBridgeTest, HandlerRegisteredWhilePausedSeesThePause) {
  bridge_->onAboveWriteBufferHighWatermark();

  bridge_->setReplayHandler(handler_);
  EXPECT_TRUE(bridge_->replayPaused());
  EXPECT_EQ(handler_.resumed_, 0);

  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->replayPaused());
  EXPECT_EQ(handler_.resumed_, 1);
}

// An unrecoverable buffer error is surfaced as a 500 local reply.
TEST_F(DecoderFilterChainBridgeTest, UnrecoverableErrorSendsLocalReply) {
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                         "ai_protocol_manager_external_buffer_error"));
  bridge_->onUnrecoverableError();
  EXPECT_EQ(stats_.request_external_buffer_error_.value(), 1);
}

// EncoderFilterChainBridge maps the bridge surface onto StreamEncoderFilterCallbacks
// but subscribes to downstream watermarks through the decoder callbacks.
class EncoderFilterChainBridgeTest : public testing::Test {
public:
  // The limit comes from the *encoder* callbacks, via encoderBufferLimit().
  EncoderFilterChainBridgeTest() {
    EXPECT_CALL(encoder_callbacks_, bufferLimit()).WillOnce(Return(100));
    EXPECT_CALL(decoder_callbacks_, addDownstreamWatermarkCallbacks(_))
        .WillOnce(Invoke([this](Http::DownstreamWatermarkCallbacks& cb) { subscriber_ = &cb; }));
    bridge_ =
        std::make_unique<EncoderFilterChainBridge>(encoder_callbacks_, decoder_callbacks_, stats_);
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  AiProtocolManagerStats stats_{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), ""))};
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::unique_ptr<EncoderFilterChainBridge> bridge_;
  Http::DownstreamWatermarkCallbacks* subscriber_{nullptr};
  RecordingHandler handler_;
};

TEST_F(EncoderFilterChainBridgeTest, DispatcherIsTheFilterCallbacksDispatcher) {
  EXPECT_EQ(&bridge_->dispatcher(), &encoder_callbacks_.dispatcher_);
}

TEST_F(EncoderFilterChainBridgeTest, InjectsNonTerminalEncodedData) {
  Buffer::OwnedImpl data("chunk");
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, /*end_stream=*/false));
  bridge_->injectData(data);
}

TEST_F(EncoderFilterChainBridgeTest, IngestBackpressureDrivesEncoderWriteBuffer) {
  EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());

  EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());
}

// The subscription goes through the *decoder* callbacks, and lasts the bridge's whole life.
TEST_F(EncoderFilterChainBridgeTest, SubscribesAtConstructionAndUnsubscribesOnDetach) {
  EXPECT_EQ(subscriber_, bridge_.get());
  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();
}

TEST_F(EncoderFilterChainBridgeTest, ResumesTheHandlerWhenWatermarksDrain) {
  bridge_->setReplayHandler(handler_);

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->replayPaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->replayPaused());
  EXPECT_EQ(handler_.resumed_, 1);

  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();

  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.resumed_, 1);
}

TEST_F(EncoderFilterChainBridgeTest, WatermarksWithNoHandlerAreNoOps) {
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->replayPaused());
}

// On the response path the error is surfaced through the encoder callbacks'
// sendLocalReply (best-effort, since the response may already be in flight).
TEST_F(EncoderFilterChainBridgeTest, UnrecoverableErrorSendsLocalReply) {
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                                 "ai_protocol_manager_external_buffer_error"));
  bridge_->onUnrecoverableError();
  EXPECT_EQ(stats_.response_external_buffer_error_.value(), 1);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

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
        .WillOnce([this](Http::UpstreamWatermarkCallbacks& cb) { subscriber_ = &cb; });
    bridge_ = std::make_unique<DecoderFilterChainBridge>(callbacks_, stats_);
  }

  void armResumeCallback() {
    bridge_->setWriteResumeCallback([this]() { ++resumed_; });
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  AiProtocolManagerStats stats_{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), ""))};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::unique_ptr<DecoderFilterChainBridge> bridge_;
  Http::UpstreamWatermarkCallbacks* subscriber_{nullptr};
  int resumed_{0};
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

// If the filter callbacks do not synchronously echo pauseSource()/resumeSource() (e.g. AsyncStream
// or teardown), subsequent genuine upstream watermarks must not be swallowed.
TEST_F(DecoderFilterChainBridgeTest, NonEchoingSourcePauseDoesNotSwallowGenuineWatermark) {
  armResumeCallback();

  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  armResumeCallback();
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 2);
}

// The bridge subscribes for its whole life rather than per callback, so the depth it reports is
// continuous; detaching unsubscribes.
TEST_F(DecoderFilterChainBridgeTest, SubscribesAtConstructionAndUnsubscribesOnDetach) {
  EXPECT_EQ(subscriber_, bridge_.get());
  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();
}

// The drain edge invokes the write-resume callback; detaching silences it.
TEST_F(DecoderFilterChainBridgeTest, ResumesCallbackWhenWatermarksDrain) {
  armResumeCallback();

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  armResumeCallback();
  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();

  // The depth keeps tracking, but with no callback registered nothing is notified.
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(resumed_, 1);
}

// Watermarks delivered before a callback is set are tracked but notify nobody.
TEST_F(DecoderFilterChainBridgeTest, WatermarksWithNoCallbackAreNoOps) {
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
}

// Pause is a level the bridge holds, not an edge it delivers: a callback registered while the
// chain is already paused sees that state, and the drain that follows reaches it.
TEST_F(DecoderFilterChainBridgeTest, CallbackRegisteredWhilePausedSeesThePause) {
  bridge_->onAboveWriteBufferHighWatermark();

  armResumeCallback();
  EXPECT_TRUE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 0);

  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// An unrecoverable buffer error is surfaced as a 500 local reply.
TEST_F(DecoderFilterChainBridgeTest, UnrecoverableErrorSendsLocalReply) {
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                         "ai_protocol_manager_external_buffer_error"));
  bridge_->onUnrecoverableError();
  EXPECT_EQ(stats_.request_external_buffer_error_.value(), 1);
}

// Both DownstreamFilterManager and UpstreamFilterManager fan the decoder's own high/low watermark
// calls out to every upstream watermark subscriber, including this bridge. Wire the mock to do the
// same.
class DecoderFilterChainBridgeEchoTest : public DecoderFilterChainBridgeTest {
public:
  DecoderFilterChainBridgeEchoTest() {
    ON_CALL(callbacks_, onDecoderFilterAboveWriteBufferHighWatermark()).WillByDefault([this] {
      subscriber_->onAboveWriteBufferHighWatermark();
    });
    ON_CALL(callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).WillByDefault([this] {
      subscriber_->onBelowWriteBufferLowWatermark();
    });
    armResumeCallback();
  }
};

// Pausing the source for ingest back-pressure must not pause output emission through the echo on a
// shared bridge. Resuming must not produce a spurious resume notification either.
TEST_F(DecoderFilterChainBridgeEchoTest, OwnSourcePauseDoesNotPauseWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 0);
}

// Genuine upstream back-pressure arriving while ingest is paused still pauses and resumes write;
// only the bridge's own echoes are filtered.
TEST_F(DecoderFilterChainBridgeEchoTest, GenuineUpstreamWatermarkStillPausesWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// A pause nested inside our own resume keeps the echoes balanced, so output emission is never
// paused.
TEST_F(DecoderFilterChainBridgeEchoTest, PauseNestedInResumeStaysBalanced) {
  bridge_->addUnacked(200);
  ON_CALL(callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).WillByDefault([this] {
    bridge_->addUnacked(200);
    subscriber_->onBelowWriteBufferLowWatermark();
  });

  bridge_->releaseUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 0);
}

// A genuine upstream high during our own pause still pauses output emission.
TEST_F(DecoderFilterChainBridgeEchoTest, GenuineWatermarkDuringOwnPauseCounts) {
  ON_CALL(callbacks_, onDecoderFilterAboveWriteBufferHighWatermark()).WillByDefault([this] {
    subscriber_->onAboveWriteBufferHighWatermark();
    subscriber_->onAboveWriteBufferHighWatermark();
  });

  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// If resumeSource() does not synchronously echo, a subsequent genuine low watermark must still
// resume write rather than staying deadlocked.
TEST_F(DecoderFilterChainBridgeEchoTest, NonEchoingResumeDoesNotDeadlockWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  EXPECT_CALL(callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).WillOnce([]() {});
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// EncoderFilterChainBridge maps the bridge surface onto StreamEncoderFilterCallbacks
// but subscribes to downstream watermarks through the decoder callbacks.
class EncoderFilterChainBridgeTest : public testing::Test {
public:
  // The limit comes from the *encoder* callbacks, via encoderBufferLimit().
  EncoderFilterChainBridgeTest() {
    EXPECT_CALL(encoder_callbacks_, bufferLimit()).WillOnce(Return(100));
    EXPECT_CALL(decoder_callbacks_, addDownstreamWatermarkCallbacks(_))
        .WillOnce([this](Http::DownstreamWatermarkCallbacks& cb) { subscriber_ = &cb; });
    bridge_ =
        std::make_unique<EncoderFilterChainBridge>(encoder_callbacks_, decoder_callbacks_, stats_);
  }

  void armResumeCallback() {
    bridge_->setWriteResumeCallback([this]() { ++resumed_; });
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  AiProtocolManagerStats stats_{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), ""))};
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::unique_ptr<EncoderFilterChainBridge> bridge_;
  Http::DownstreamWatermarkCallbacks* subscriber_{nullptr};
  int resumed_{0};
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

// If the filter callbacks do not synchronously echo pauseSource()/resumeSource(), subsequent
// genuine downstream watermarks must not be swallowed.
TEST_F(EncoderFilterChainBridgeTest, NonEchoingSourcePauseDoesNotSwallowGenuineWatermark) {
  armResumeCallback();

  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  armResumeCallback();
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 2);
}

// The subscription goes through the *decoder* callbacks, and lasts the bridge's whole life.
TEST_F(EncoderFilterChainBridgeTest, SubscribesAtConstructionAndUnsubscribesOnDetach) {
  EXPECT_EQ(subscriber_, bridge_.get());
  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();
}

TEST_F(EncoderFilterChainBridgeTest, ResumesCallbackWhenWatermarksDrain) {
  armResumeCallback();

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  armResumeCallback();
  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(Ref(*bridge_)));
  bridge_->detachFromFilterChain();

  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(resumed_, 1);
}

TEST_F(EncoderFilterChainBridgeTest, WatermarksWithNoCallbackAreNoOps) {
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
}

// The filter manager fans the encoder's own high/low watermark calls out to every downstream
// watermark subscriber, including this bridge. Wires the mock to do the same.
class EncoderFilterChainBridgeEchoTest : public EncoderFilterChainBridgeTest {
public:
  EncoderFilterChainBridgeEchoTest() {
    ON_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark())
        .WillByDefault([this] { subscriber_->onAboveWriteBufferHighWatermark(); });
    ON_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark())
        .WillByDefault([this] { subscriber_->onBelowWriteBufferLowWatermark(); });
    armResumeCallback();
  }
};

// Pausing the source for ingest back-pressure must not pause output emission through the echo:
// emitting output is what drains the charged bytes, so that would deadlock. Resuming must not
// produce a spurious resume notification either.
TEST_F(EncoderFilterChainBridgeEchoTest, OwnSourcePauseDoesNotPauseWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 0);
}

// Genuine downstream back-pressure arriving while ingest is paused still pauses and resumes
// output emission; only the bridge's own echoes are filtered.
TEST_F(EncoderFilterChainBridgeEchoTest, GenuineDownstreamWatermarkStillPausesWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);

  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// A pause nested inside our own resume keeps the echoes balanced, so output emission is never
// paused.
TEST_F(EncoderFilterChainBridgeEchoTest, PauseNestedInResumeStaysBalanced) {
  bridge_->addUnacked(200);
  ON_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark()).WillByDefault([this] {
    bridge_->addUnacked(200);
    subscriber_->onBelowWriteBufferLowWatermark();
  });

  bridge_->releaseUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 0);
}

// A genuine downstream high during our own pause still pauses output emission.
TEST_F(EncoderFilterChainBridgeEchoTest, GenuineWatermarkDuringOwnPauseCounts) {
  ON_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark()).WillByDefault([this] {
    subscriber_->onAboveWriteBufferHighWatermark();
    subscriber_->onAboveWriteBufferHighWatermark();
  });

  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// If resumeSource() does not synchronously echo, a subsequent genuine low watermark must still
// resume write rather than staying deadlocked.
TEST_F(EncoderFilterChainBridgeEchoTest, NonEchoingResumeDoesNotDeadlockWrite) {
  bridge_->addUnacked(200);
  EXPECT_TRUE(bridge_->ingestPaused());
  EXPECT_FALSE(bridge_->writePaused());

  EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark()).WillOnce([]() {});
  bridge_->releaseUnacked(200);
  EXPECT_FALSE(bridge_->ingestPaused());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_TRUE(bridge_->writePaused());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_FALSE(bridge_->writePaused());
  EXPECT_EQ(resumed_, 1);
}

// On the response path the error is surfaced through the encoder callbacks' sendLocalReply
// (best-effort, since the response may already be in flight).
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

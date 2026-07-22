#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "test/mocks/http/mocks.h"
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

// Records the watermark callbacks the bridge forwards, so a test can verify the
// path-specific Envoy watermarks reach the BufferManager's handler interface.
class RecordingHandler : public ReplayWatermarkHandler {
public:
  void onReplayAboveHighWatermark() override { ++above_; }
  void onReplayBelowLowWatermark() override { ++below_; }
  int above_{0};
  int below_{0};
};

// DecoderFilterChainBridge maps the path-agnostic bridge surface onto
// StreamDecoderFilterCallbacks and forwards upstream watermarks.
class DecoderFilterChainBridgeTest : public testing::Test {
public:
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  DecoderFilterChainBridge bridge_{callbacks_};
  RecordingHandler handler_;
};

TEST_F(DecoderFilterChainBridgeTest, DispatcherAndBufferLimit) {
  EXPECT_EQ(&bridge_.dispatcher(), &callbacks_.dispatcher_);
  // decoderBufferLimit() is a non-virtual wrapper over the virtual bufferLimit().
  EXPECT_CALL(callbacks_, bufferLimit()).WillOnce(Return(4096));
  EXPECT_EQ(bridge_.bufferLimit(), 4096);
}

TEST_F(DecoderFilterChainBridgeTest, InjectsNonTerminalDecodedData) {
  Buffer::OwnedImpl data("chunk");
  EXPECT_CALL(callbacks_, injectDecodedDataToFilterChain(_, /*end_stream=*/false));
  bridge_.injectData(data);
}

TEST_F(DecoderFilterChainBridgeTest, PauseAndResumeDriveDecoderWriteBuffer) {
  EXPECT_CALL(callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  bridge_.pauseSource();
  EXPECT_CALL(callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  bridge_.resumeSource();
}

// Registering subscribes to upstream watermarks and forwards them to the handler;
// unregistering removes the subscription and silences forwarding.
TEST_F(DecoderFilterChainBridgeTest, RegistersForwardsAndUnregisters) {
  EXPECT_CALL(callbacks_, addUpstreamWatermarkCallbacks(Ref(bridge_)));
  bridge_.registerReplayWatermarks(handler_);

  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.above_, 1);
  EXPECT_EQ(handler_.below_, 1);

  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(Ref(bridge_)));
  bridge_.unregisterReplayWatermarks();

  // After unregister the handler is detached; further watermarks are dropped.
  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.above_, 1);
  EXPECT_EQ(handler_.below_, 1);
}

// Watermarks delivered before any registration (handler_ is null) are no-ops.
TEST_F(DecoderFilterChainBridgeTest, WatermarksBeforeRegisterAreNoOps) {
  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  SUCCEED();
}

// Unregister without a prior register must not call removeUpstreamWatermarkCallbacks.
TEST_F(DecoderFilterChainBridgeTest, UnregisterWithoutRegisterIsNoOp) {
  EXPECT_CALL(callbacks_, removeUpstreamWatermarkCallbacks(_)).Times(0);
  bridge_.unregisterReplayWatermarks();
}

// An unrecoverable buffer error is surfaced as a 500 local reply.
TEST_F(DecoderFilterChainBridgeTest, UnrecoverableErrorSendsLocalReply) {
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                         "ai_protocol_manager_external_buffer_error"));
  bridge_.onUnrecoverableError();
}

// EncoderFilterChainBridge maps the bridge surface onto StreamEncoderFilterCallbacks
// but subscribes to downstream watermarks through the decoder callbacks.
class EncoderFilterChainBridgeTest : public testing::Test {
public:
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  EncoderFilterChainBridge bridge_{encoder_callbacks_, decoder_callbacks_};
  RecordingHandler handler_;
};

TEST_F(EncoderFilterChainBridgeTest, DispatcherAndBufferLimit) {
  EXPECT_EQ(&bridge_.dispatcher(), &encoder_callbacks_.dispatcher_);
  // encoderBufferLimit() is a non-virtual wrapper over the virtual bufferLimit().
  EXPECT_CALL(encoder_callbacks_, bufferLimit()).WillOnce(Return(8192));
  EXPECT_EQ(bridge_.bufferLimit(), 8192);
}

TEST_F(EncoderFilterChainBridgeTest, InjectsNonTerminalEncodedData) {
  Buffer::OwnedImpl data("chunk");
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, /*end_stream=*/false));
  bridge_.injectData(data);
}

TEST_F(EncoderFilterChainBridgeTest, PauseAndResumeDriveEncoderWriteBuffer) {
  EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  bridge_.pauseSource();
  EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  bridge_.resumeSource();
}

// Registering subscribes to downstream watermarks (via the decoder callbacks) and
// forwards them; unregistering removes the subscription and silences forwarding.
TEST_F(EncoderFilterChainBridgeTest, RegistersForwardsAndUnregisters) {
  EXPECT_CALL(decoder_callbacks_, addDownstreamWatermarkCallbacks(Ref(bridge_)));
  bridge_.registerReplayWatermarks(handler_);

  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.above_, 1);
  EXPECT_EQ(handler_.below_, 1);

  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(Ref(bridge_)));
  bridge_.unregisterReplayWatermarks();

  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  EXPECT_EQ(handler_.above_, 1);
  EXPECT_EQ(handler_.below_, 1);
}

TEST_F(EncoderFilterChainBridgeTest, WatermarksBeforeRegisterAreNoOps) {
  bridge_.onAboveWriteBufferHighWatermark();
  bridge_.onBelowWriteBufferLowWatermark();
  SUCCEED();
}

TEST_F(EncoderFilterChainBridgeTest, UnregisterWithoutRegisterIsNoOp) {
  EXPECT_CALL(decoder_callbacks_, removeDownstreamWatermarkCallbacks(_)).Times(0);
  bridge_.unregisterReplayWatermarks();
}

// On the response path the error is surfaced through the encoder callbacks'
// sendLocalReply (best-effort, since the response may already be in flight).
TEST_F(EncoderFilterChainBridgeTest, UnrecoverableErrorSendsLocalReply) {
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                                 "ai_protocol_manager_external_buffer_error"));
  bridge_.onUnrecoverableError();
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

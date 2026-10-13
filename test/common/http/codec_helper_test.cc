#include "source/common/http/codec_helper.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Mock;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {

class TestStreamCallbackHelper : public StreamCallbackHelper {
public:
  using StreamCallbackHelper::addCallbacksHelper;
};

TEST(StreamCallbackHelperTest, LowWatermarkWithoutHighWatermarkDoesNotUnderflowCounter) {
  TestStreamCallbackHelper helper;
  StrictMock<MockStreamCallbacks> callbacks;
  helper.addCallbacksHelper(callbacks);

  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_ENVOY_BUG(
      helper.runLowWatermarkCallbacks(),
      "HTTP stream low watermark callback without a preceding high watermark callback");
  Mock::VerifyAndClearExpectations(&callbacks);

  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  helper.runHighWatermarkCallbacks();
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  helper.runLowWatermarkCallbacks();
}

} // namespace
} // namespace Http
} // namespace Envoy

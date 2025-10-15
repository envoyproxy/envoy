#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;

class McpFilterTest : public testing::Test {
public:
  McpFilterTest() {
    config_ = std::make_shared<McpFilterConfig>();
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

protected:
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  McpFilterConfigSharedPtr config_;
  std::unique_ptr<McpFilter> filter_;
};

TEST_F(McpFilterTest, DecodeHeaders) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

TEST_F(McpFilterTest, DecodeData) {
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
}

TEST_F(McpFilterTest, EncodeHeaders) {
  Http::TestResponseHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
}

TEST_F(McpFilterTest, EncodeData) {
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include <memory>

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"

#include "source/extensions/filters/http/body_size_limit/body_size_limit_filter.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

class BodySizeLimitFilterTest : public testing::Test {
public:
  BodySizeLimitFilterConfigSharedPtr setupConfig(uint32_t max_request_bytes = 1024 * 1024) {
    envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit proto_config;
    proto_config.mutable_max_request_bytes()->set_value(max_request_bytes);
    return std::make_shared<BodySizeLimitFilterConfig>(proto_config);
  }

  BodySizeLimitFilterTest() : config_(setupConfig()), filter_(config_) {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  BodySizeLimitFilterConfigSharedPtr config_;
  BodySizeLimitFilter filter_;
};

TEST_F(BodySizeLimitFilterTest, HeaderOnlyRequest) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(BodySizeLimitFilterTest, TestMetadata) {
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
}

TEST_F(BodySizeLimitFilterTest, RequestWithData) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));
}

TEST_F(BodySizeLimitFilterTest, TxResetAfterEndStream) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));

  // It's possible that the stream will be reset on the TX side even after RX end stream. Mimic
  // that here.
  filter_.onDestroy();
}

TEST_F(BodySizeLimitFilterTest, ContentLengthPopulationAlreadyPresent) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers{{"content-length", "3"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ(headers.getContentLengthValue(), "3");
}

TEST_F(BodySizeLimitFilterTest, ContentLengthTooLarge) {
  auto small_config = setupConfig(123);
  BodySizeLimitFilter small_filter(small_config);
  small_filter.setDecoderFilterCallbacks(callbacks_);

  Http::TestRequestHeaderMapImpl headers{{"content-length", "1233"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, small_filter.decodeHeaders(headers, false));
}

TEST_F(BodySizeLimitFilterTest, DataTooLong) {
  auto small_config = setupConfig(5);
  BodySizeLimitFilter small_filter(small_config);
  small_filter.setDecoderFilterCallbacks(callbacks_);

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, small_filter.decodeHeaders(headers, false));
  Buffer::OwnedImpl data1("Hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, small_filter.decodeData(data1, false));
  Buffer::OwnedImpl data2(", world!");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, small_filter.decodeData(data2, false));
}

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

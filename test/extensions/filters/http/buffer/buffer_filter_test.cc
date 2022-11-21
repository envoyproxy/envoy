#include <chrono>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/extensions/filters/http/buffer/buffer_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

class BufferFilterTest : public testing::Test {
public:
  BufferFilterConfigSharedPtr setupConfig() {
    envoy::extensions::filters::http::buffer::v3::Buffer proto_config;
    proto_config.mutable_max_request_bytes()->set_value(1024 * 1024);
    return std::make_shared<BufferFilterConfig>(proto_config);
  }

  BufferFilterTest() : config_(setupConfig()), filter_(config_) {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  BufferFilterConfigSharedPtr config_;
  BufferFilter filter_;
};

TEST_F(BufferFilterTest, HeaderOnlyRequest) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(BufferFilterTest, TestMetadata) {
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
}

TEST_F(BufferFilterTest, RequestWithData) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));
}

TEST_F(BufferFilterTest, TxResetAfterEndStream) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));

  // It's possible that the stream will be reset on the TX side even after RX end stream. Mimic
  // that here.
  filter_.onDestroy();
}

TEST_F(BufferFilterTest, ContentLengthPopulation) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));
  EXPECT_EQ(headers.getContentLengthValue(), "11");
}

TEST_F(BufferFilterTest, ContentLengthPopulationInTrailers) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));
  ASSERT_EQ(headers.ContentLength(), nullptr);

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(trailers));
  EXPECT_EQ(headers.getContentLengthValue(), "5");
}

TEST_F(BufferFilterTest, ContentLengthPopulationAlreadyPresent) {
  InSequence s;

  Http::TestRequestHeaderMapImpl headers{{"content-length", "3"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ(headers.getContentLengthValue(), "3");
}

TEST_F(BufferFilterTest, PerFilterConfigOverride) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute per_route_cfg;
  auto* buf = per_route_cfg.mutable_buffer();
  buf->mutable_max_request_bytes()->set_value(123);
  BufferFilterSettings route_settings(per_route_cfg);

  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_)).WillOnce(Return(&route_settings));
  EXPECT_CALL(callbacks_, setDecoderBufferLimit(123ULL));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  filter_.onDestroy();
}

TEST_F(BufferFilterTest, PerFilterConfigDisabledConfigOverride) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute per_route_cfg;
  per_route_cfg.set_disabled(true);
  BufferFilterSettings route_settings(per_route_cfg);

  EXPECT_CALL(*callbacks_.route_, mostSpecificPerFilterConfig(_)).WillOnce(Return(&route_settings));
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));
  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data1, true));
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

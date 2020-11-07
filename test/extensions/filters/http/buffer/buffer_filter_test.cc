#include <chrono>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"

#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"

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

  void routeLocalConfig(const Router::RouteSpecificFilterConfig* route_settings,
                        const Router::RouteSpecificFilterConfig* vhost_settings) {
    ON_CALL(callbacks_.route_->route_entry_, perFilterConfig(HttpFilterNames::get().Buffer))
        .WillByDefault(Return(route_settings));
    ON_CALL(callbacks_.route_->route_entry_.virtual_host_,
            perFilterConfig(HttpFilterNames::get().Buffer))
        .WillByDefault(Return(vhost_settings));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  BufferFilterConfigSharedPtr config_;
  BufferFilter filter_;
  // Create a runtime loader, so that tests can manually manipulate runtime guarded features.
  TestScopedRuntime scoped_runtime;
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

TEST_F(BufferFilterTest, RouteConfigOverride) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute route_cfg;
  auto* buf = route_cfg.mutable_buffer();
  buf->mutable_max_request_bytes()->set_value(123);
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute vhost_cfg;
  vhost_cfg.set_disabled(true);
  BufferFilterSettings route_settings(route_cfg);
  BufferFilterSettings vhost_settings(vhost_cfg);
  routeLocalConfig(&route_settings, &vhost_settings);

  EXPECT_CALL(callbacks_, setDecoderBufferLimit(123ULL));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  filter_.onDestroy();
}

TEST_F(BufferFilterTest, VHostConfigOverride) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute vhost_cfg;
  auto* buf = vhost_cfg.mutable_buffer();
  buf->mutable_max_request_bytes()->set_value(789);
  BufferFilterSettings vhost_settings(vhost_cfg);
  routeLocalConfig(nullptr, &vhost_settings);

  EXPECT_CALL(callbacks_, setDecoderBufferLimit(789ULL));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));
  filter_.onDestroy();
}

TEST_F(BufferFilterTest, RouteDisabledConfigOverride) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute vhost_cfg;
  vhost_cfg.set_disabled(true);
  BufferFilterSettings vhost_settings(vhost_cfg);
  routeLocalConfig(nullptr, &vhost_settings);

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

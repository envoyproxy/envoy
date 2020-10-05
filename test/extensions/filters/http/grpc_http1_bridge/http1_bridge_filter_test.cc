#include "common/buffer/buffer_impl.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {
namespace {

class GrpcHttp1BridgeFilterTest : public testing::Test {
public:
  GrpcHttp1BridgeFilterTest() : context_(*symbol_table_), filter_(context_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
    ON_CALL(decoder_callbacks_.stream_info_, protocol()).WillByDefault(ReturnPointee(&protocol_));
  }

  ~GrpcHttp1BridgeFilterTest() override { filter_.onDestroy(); }

  Stats::TestSymbolTable symbol_table_;
  Grpc::ContextImpl context_;
  Http1BridgeFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  absl::optional<Http::Protocol> protocol_{Http::Protocol::Http11};
};

TEST_F(GrpcHttp1BridgeFilterTest, NoRoute) {
  protocol_ = Http::Protocol::Http2;
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, NoCluster) {
  protocol_ = Http::Protocol::Http2;
  ON_CALL(decoder_callbacks_, clusterInfo()).WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, StatsHttp2HeaderOnlyResponse) {
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, true));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.failure")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcHttp1BridgeFilterTest, StatsHttp2NormalResponse) {
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcHttp1BridgeFilterTest, StatsHttp2ContentTypeGrpcPlusProto) {
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp2) {
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp1) {
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingNormalResponse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl("hello"));
  ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Return(buffer.get()));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
  EXPECT_EQ("5", response_headers.get_("content-length"));
  EXPECT_EQ("0", response_headers.get_("grpc-status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingBadGrpcStatus) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "1"}, {"grpc-message", "foo"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("503", response_headers.get_(":status"));
  EXPECT_EQ("0", response_headers.get_("content-length"));
  EXPECT_EQ("1", response_headers.get_("grpc-status"));
  EXPECT_EQ("foo", response_headers.get_("grpc-message"));
}

} // namespace
} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

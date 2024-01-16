#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/http/grpc_http1_bridge/config.h"
#include "source/extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

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

using FilterPtr = std::unique_ptr<Http1BridgeFilter>;

namespace {

class GrpcHttp1BridgeFilterTest : public testing::Test {
protected:
  void initialize(bool upgrade_protobuf = false, bool ignore_query_params = false) {
    envoy::extensions::filters::http::grpc_http1_bridge::v3::Config config;
    config.set_upgrade_protobuf_to_grpc(upgrade_protobuf);
    config.set_ignore_query_parameters(ignore_query_params);
    filter_ = std::make_unique<Http1BridgeFilter>(context_, config);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    ON_CALL(decoder_callbacks_.stream_info_, protocol()).WillByDefault(ReturnPointee(&protocol_));
  }

public:
  GrpcHttp1BridgeFilterTest() : context_(*symbol_table_) {}

  ~GrpcHttp1BridgeFilterTest() override { filter_->onDestroy(); }

  FilterPtr filter_;
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Grpc::ContextImpl context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  absl::optional<Http::Protocol> protocol_{Http::Protocol::Http11};
};

TEST_F(GrpcHttp1BridgeFilterTest, NoRoute) {
  initialize();
  protocol_ = Http::Protocol::Http2;
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, NoCluster) {
  initialize();
  protocol_ = Http::Protocol::Http2;
  ON_CALL(decoder_callbacks_, clusterInfo()).WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, Http2HeaderOnlyResponse) {
  initialize();
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(continue_headers));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.failure"));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.total"));
}

TEST_F(GrpcHttp1BridgeFilterTest, Http2NormalResponse) {
  initialize();
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.success"));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.total"));
}

TEST_F(GrpcHttp1BridgeFilterTest, Http2ContentTypeGrpcPlusProto) {
  initialize();
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.success"));
  EXPECT_FALSE(
      stats_store_.findCounterByString("grpc.lyft.users.BadCompanions.GetBadCompanions.total"));
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp2) {
  initialize();
  protocol_ = Http::Protocol::Http2;

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp1) {
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingNormalResponse) {
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl("hello"));
  ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Return(buffer.get()));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
  EXPECT_EQ("5", response_headers.get_("content-length"));
  EXPECT_EQ("0", response_headers.get_("grpc-status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingBadGrpcStatus) {
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "1"}, {"grpc-message", "foo"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("503", response_headers.get_(":status"));
  EXPECT_EQ("0", response_headers.get_("content-length"));
  EXPECT_EQ("1", response_headers.get_("grpc-status"));
  EXPECT_EQ("foo", response_headers.get_("grpc-message"));
}

TEST_F(GrpcHttp1BridgeFilterTest, ProtobufNotUpgradedToGrpc) {
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/x-protobuf"},
                                                 {":path", "/v1/spotify.Concat/Concat"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

// Verifies that requests with protobuf content are framed as gRPC when the filter is configured as
// such
TEST_F(GrpcHttp1BridgeFilterTest, ProtobufUpgradedToGrpc) {
  initialize(true);
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/x-protobuf"},
                                                 {":path", "/v1/spotify.Concat/Concat"}};
  Buffer::OwnedImpl data("helloworld");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, request_headers.getContentTypeValue());

  EXPECT_CALL(decoder_callbacks_,
              addDecodedData(_, true)) // todo: find a better way of testing this
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) { ASSERT_EQ(data.length(), d.length()); })));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  Http::TestRequestTrailerMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(data, false));
  EXPECT_EQ("world", data.toString());
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(data, false));
  EXPECT_EQ("world", data.toString());
  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

// Verifies that the Content-Length header is removed during a protobuf upgrade
TEST_F(GrpcHttp1BridgeFilterTest, ProtobufUpgradedHeaderSanitized) {
  initialize(true);
  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/x-protobuf"},
                                                 {"content-length", "5"},
                                                 {":path", "/v1/spotify.Concat/Concat"}};

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, request_headers.getContentTypeValue());
  EXPECT_EQ("", request_headers.getContentLengthValue());
}

// Verifies that the query params in URL are removed when ignore_query_parameters is enabled
TEST_F(GrpcHttp1BridgeFilterTest, QueryParamsIgnored) {
  initialize(false, true);
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/v1/spotify.Concat/Concat?timestamp=1701678591"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("/v1/spotify.Concat/Concat", request_headers.getPathValue());
}

} // namespace
} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "common/buffer/buffer_impl.h"
#include "common/grpc/http1_bridge_filter.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Grpc {

class GrpcHttp1BridgeFilterTest : public testing::Test {
public:
  GrpcHttp1BridgeFilterTest() : filter_(cm_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
    ON_CALL(decoder_callbacks_.request_info_, protocol()).WillByDefault(ReturnRef(protocol_));
  }

  ~GrpcHttp1BridgeFilterTest() { filter_.onDestroy(); }

  NiceMock<Upstream::MockClusterManager> cm_;
  Http1BridgeFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Optional<Http::Protocol> protocol_{Http::Protocol::Http11};
};

TEST_F(GrpcHttp1BridgeFilterTest, NoRoute) {
  protocol_ = Http::Protocol::Http2;
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, NoCluster) {
  protocol_ = Http::Protocol::Http2;
  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl response_headers{{":status", "404"}};
}

TEST_F(GrpcHttp1BridgeFilterTest, StatsHttp2HeaderOnlyResponse) {
  protocol_ = Http::Protocol::Http2;

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, true));
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.failure")
                     .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcHttp1BridgeFilterTest, StatsHttp2NormalResponse) {
  protocol_ = Http::Protocol::Http2;

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp2) {
  protocol_ = Http::Protocol::Http2;

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestHeaderMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, NotHandlingHttp1) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestHeaderMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingNormalResponse) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestHeaderMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl("hello"));
  ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Return(buffer.get()));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("200", response_headers.get_(":status"));
  EXPECT_EQ("5", response_headers.get_("content-length"));
  EXPECT_EQ("0", response_headers.get_("grpc-status"));
}

TEST_F(GrpcHttp1BridgeFilterTest, HandlingBadGrpcStatus) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  Http::TestHeaderMapImpl request_trailers{{"hello", "world"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "1"}, {"grpc-message", "foo"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ("503", response_headers.get_(":status"));
  EXPECT_EQ("0", response_headers.get_("content-length"));
  EXPECT_EQ("1", response_headers.get_("grpc-status"));
  EXPECT_EQ("foo", response_headers.get_("grpc-message"));
}

} // namespace Grpc
} // namespace Envoy

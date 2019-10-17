#include "common/grpc/common.h"

#include "extensions/filters/http/grpc_stats/config.h"

#include "test/common/buffer/utility.h"
#include "test/common/stream_info/test_util.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {
namespace {

class GrpcStatsFilterConfigTest : public testing::Test {
protected:
  void SetUp() override {
    std::string json_string = "{}";
    Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
    GrpcStatsFilterConfig factory;
    Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context_);
    Http::MockFilterChainFactoryCallbacks filter_callback;

    ON_CALL(filter_callback, addStreamFilter(_)).WillByDefault(testing::SaveArg<0>(&filter_));
    EXPECT_CALL(filter_callback, addStreamFilter(_));
    cb(filter_callback);

    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<Http::StreamFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  TestStreamInfo stream_info_;
};

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2HeaderOnlyResponse) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(continue_headers));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.failure")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2NormalResponse) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2ContentTypeGrpcPlusProto) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc+proto"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, MessageCounts) {
  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc+proto"},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  ProtobufWkt::Value v1;
  v1.set_string_value("v1");
  auto b1 = Grpc::Common::serializeToGrpcFrame(v1);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*b1, false));
  ProtobufWkt::Value v2;
  v2.set_string_value("v2");
  auto b2 = Grpc::Common::serializeToGrpcFrame(v2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*b2, true));

  auto& data = stream_info_.filterState().getDataMutable<GrpcMessageCounterObject>(
      HttpFilterNames::get().GrpcStats);
  EXPECT_EQ(2U, data.request_message_count);
  EXPECT_EQ(0U, data.response_message_count);
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.request_count")
                    .value());
  EXPECT_EQ(0U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.response_count")
                    .value());

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc+proto"},
                                           {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add(*b1);
  buffer.add(*b2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  data = stream_info_.filterState().getDataMutable<GrpcMessageCounterObject>(
      HttpFilterNames::get().GrpcStats);
  EXPECT_EQ(2U, data.request_message_count);
  EXPECT_EQ(2U, data.response_message_count);
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.request_count")
                    .value());
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.response_count")
                    .value());

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*b1, true));
  data = stream_info_.filterState().getDataMutable<GrpcMessageCounterObject>(
      HttpFilterNames::get().GrpcStats);
  EXPECT_EQ(2U, data.request_message_count);
  EXPECT_EQ(3U, data.response_message_count);
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.request_count")
                    .value());
  EXPECT_EQ(3U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("grpc.lyft.users.BadCompanions.GetBadCompanions.response_count")
                    .value());
}

} // namespace
} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

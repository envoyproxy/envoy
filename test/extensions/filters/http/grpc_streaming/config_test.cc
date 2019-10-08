#include "common/grpc/common.h"

#include "extensions/filters/http/grpc_streaming/config.h"

#include "test/common/buffer/utility.h"
#include "test/common/stream_info/test_util.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {
namespace {

TEST(GrpcStreamingFilterConfigTest, FilterFactory) {
  std::string json_string = "{}";
  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcStreamingFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;

  std::shared_ptr<Http::StreamFilter> filter;
  ON_CALL(filter_callback, addStreamFilter(_)).WillByDefault(testing::SaveArg<0>(&filter));
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);

  TestStreamInfo stream_info;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  ON_CALL(callbacks, streamInfo()).WillByDefault(testing::ReturnRef(stream_info));
  filter->setDecoderFilterCallbacks(callbacks);

  Http::TestHeaderMapImpl request_headers{{"content-type", "application/grpc+proto"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));

  ProtobufWkt::Value v1;
  v1.set_string_value("v1");
  auto b1 = Grpc::Common::serializeToGrpcFrame(v1);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(*b1, false));
  ProtobufWkt::Value v2;
  v2.set_string_value("v2");
  auto b2 = Grpc::Common::serializeToGrpcFrame(v2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(*b2, true));

  auto& data = stream_info.filterState().getDataMutable<GrpcMessageCounterObject>(
      HttpFilterNames::get().GrpcStreaming);
  EXPECT_EQ(2, data.request_message_count);
  EXPECT_EQ(0, data.response_message_count);

  Http::TestHeaderMapImpl response_headers{{"content-type", "application/grpc+proto"},
                                           {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add(*b1);
  buffer.add(*b2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(buffer, true));

  data = stream_info.filterState().getDataMutable<GrpcMessageCounterObject>(
      HttpFilterNames::get().GrpcStreaming);
  EXPECT_EQ(2, data.request_message_count);
  EXPECT_EQ(2, data.response_message_count);
}

} // namespace
} // namespace GrpcStreaming
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

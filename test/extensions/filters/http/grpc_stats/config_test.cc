#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.validate.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/grpc_stats/grpc_stats_filter.h"

#include "test/common/buffer/utility.h"
#include "test/common/stream_info/test_util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Property;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {
namespace {

class GrpcStatsFilterConfigTest : public testing::Test {
protected:
  void initialize() {
    GrpcStatsFilterConfigFactory factory;
    Http::FilterFactoryCb cb =
        factory.createFilterFactoryFromProto(config_, "stats", context_).value();
    Http::MockFilterChainFactoryCallbacks filter_callback;

    ON_CALL(filter_callback, addStreamFilter(_)).WillByDefault(testing::SaveArg<0>(&filter_));
    EXPECT_CALL(filter_callback, addStreamFilter(_));
    cb(filter_callback);

    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

    ON_CALL(*decoder_callbacks_.cluster_info_, statsScope())
        .WillByDefault(testing::ReturnRef(scope_));

    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void addAllowlistEntry() {
    auto* allowlist = config_.mutable_individual_method_stats_allowlist();
    auto* services = allowlist->mutable_services();
    auto* service = services->Add();
    service->set_name("BadCompanions");
    *service->mutable_method_names()->Add() = "GetBadCompanions";
    *service->mutable_method_names()->Add() = "AnotherMethod";
  }

  void doRequestResponse(Http::TestRequestHeaderMapImpl& request_headers) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
    Buffer::OwnedImpl data("hello");
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
    Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  }

  envoy::extensions::filters::http::grpc_stats::v3::FilterConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::StreamFilterSharedPtr filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
};

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2HeaderOnlyResponse) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();
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
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.failure")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.grpc_stats"));
}

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2NormalResponse) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.grpc_stats"));
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectUnaryHeaderOnly) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/proto"},
      {"connect-protocol-version", "1"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));

  EXPECT_EQ(0U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());
  EXPECT_EQ(0U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count")
                    .value());

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectUnaryBodies) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  config_.set_emit_filter_state(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/proto"},
      {"connect-protocol-version", "1"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Buffer::OwnedImpl buffer{"{}"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  buffer = {"{}"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  const auto* data =
      stream_info_.filterState()->getDataReadOnly<GrpcStatsObject>("envoy.filters.http.grpc_stats");
  auto filter_object =
      *dynamic_cast<envoy::extensions::filters::http::grpc_stats::v3::FilterObject*>(
          data->serializeAsProto().get());
  EXPECT_EQ(1U, data->request_message_count);
  EXPECT_EQ(1U, data->response_message_count);
  EXPECT_EQ(1U, filter_object.request_message_count());
  EXPECT_EQ(1U, filter_object.response_message_count());

  EXPECT_EQ(1U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());
  EXPECT_EQ(1U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count")
                    .value());

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectStreamingOk) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/connect+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl buffer{"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectStreamingError) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/connect+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl buffer{R"({"error":{"code":"unavailable"}})"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectStreamingBrokenError) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/connect+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl buffer{R"({"error":{}})"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectStreamingInvalidJSON) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/connect+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl buffer{"{-"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsConnectStreamingFrameAfterEOS) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/connect+json"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl buffer{"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  buffer = {"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  buffer = {"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  buffer = {"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(1U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());
  EXPECT_EQ(1U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count")
                    .value());
}

TEST_F(GrpcStatsFilterConfigTest, StatsReplaceDotsInGrpcServiceName) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  config_.set_replace_dots_in_grpc_service_name(true);
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft_users_BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft_users_BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.grpc_stats"));
}

TEST_F(GrpcStatsFilterConfigTest, StatsHttp2ContentTypeGrpcPlusProto) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  initialize();
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.grpc_stats"));
}

// Test that an allowlist match results in method-named stats.
TEST_F(GrpcStatsFilterConfigTest, StatsAllowlistMatch) {
  addAllowlistEntry();
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":path", "/BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.total")
                     .value());
}

// Test that an allowlist method mismatch results in going to the generic stat.
TEST_F(GrpcStatsFilterConfigTest, StatsAllowlistMismatchMethod) {
  addAllowlistEntry();
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":path", "/BadCompanions/GetGoodCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetGoodCompanions.success")
                     .value());
  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetGoodCompanions.total")
                     .value());
  EXPECT_EQ(
      1UL,
      decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.success").value());
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.total").value());
}

// Test that an allowlist service mismatch results in going to the generic stat.
TEST_F(GrpcStatsFilterConfigTest, StatsAllowlistMismatchService) {
  addAllowlistEntry();
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":path", "/GoodCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.GoodCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.GoodCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_EQ(
      1UL,
      decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.success").value());
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.total").value());
}

// Test that any method results in going to the generic stat, when stats_for_all_methods == false.
TEST_F(GrpcStatsFilterConfigTest, DisableStatsForAllMethods) {
  config_.mutable_stats_for_all_methods()->set_value(false);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":path", "/BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_EQ(
      1UL,
      decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.success").value());
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.total").value());
}

// Test that any method results in a specific stat, when stats_for_all_methods isn't set
// at all.
TEST_F(GrpcStatsFilterConfigTest, StatsForAllMethodsDefaultSetting) {
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/grpc"},
                                                 {":path", "/BadCompanions/GetBadCompanions"}};

  doRequestResponse(request_headers);

  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(0UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc.BadCompanions.GetBadCompanions.total")
                     .value());
  EXPECT_EQ(
      1UL,
      decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.success").value());
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()->statsScope().counterFromString("grpc.total").value());
}

TEST_F(GrpcStatsFilterConfigTest, MessageCounts) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  config_.set_emit_filter_state(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
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

  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());

  // Check that there is response_message_count stat yet. We use
  // stats_store_.findCounterByString rather than looking on
  // clusterInfo()->statsScope() because findCounterByString is not an API on
  // Stats::Store, and there is no prefix so the names will match. We verify
  // that by double-checking we can find the request_message_count using the
  // same API.
  EXPECT_FALSE(stats_store_.findCounterByString(
      "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count"));
  EXPECT_TRUE(stats_store_.findCounterByString(
      "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count"));

  const auto* data =
      stream_info_.filterState()->getDataReadOnly<GrpcStatsObject>("envoy.filters.http.grpc_stats");
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(0U, data->response_message_count);

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/grpc+proto"},
                                                   {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add(*b1);
  buffer.add(*b2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count")
                    .value());
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(2U, data->response_message_count);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*b1, true));
  EXPECT_EQ(2U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.request_message_count")
                    .value());
  EXPECT_EQ(3U, decoder_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(
                        "grpc.lyft.users.BadCompanions.GetBadCompanions.response_message_count")
                    .value());
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(3U, data->response_message_count);

  auto filter_object =
      *dynamic_cast<envoy::extensions::filters::http::grpc_stats::v3::FilterObject*>(
          data->serializeAsProto().get());
  EXPECT_EQ(2U, filter_object.request_message_count());
  EXPECT_EQ(3U, filter_object.response_message_count());
  EXPECT_EQ("2,3", data->serializeAsString().value());
}

TEST_F(GrpcStatsFilterConfigTest, UpstreamStats) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  config_.set_emit_filter_state(true);
  config_.set_enable_upstream_stats(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  stream_info_.upstream_info_->upstreamTiming().last_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::nanoseconds(20000000));
  stream_info_.upstream_info_->upstreamTiming().last_upstream_rx_byte_received_ =
      MonotonicTime(std::chrono::nanoseconds(30000000));

  EXPECT_CALL(stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name,
                           "grpc.lyft.users.BadCompanions.GetBadCompanions.upstream_rq_time"),
                  10ul));

  doRequestResponse(request_headers);
}

TEST_F(GrpcStatsFilterConfigTest, UpstreamStatsWithTrailersOnly) {
  config_.mutable_stats_for_all_methods()->set_value(true);
  config_.set_emit_filter_state(true);
  config_.set_enable_upstream_stats(true);
  initialize();

  stream_info_.upstream_info_->upstreamTiming().last_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::nanoseconds(20000000));
  stream_info_.upstream_info_->upstreamTiming().last_upstream_rx_byte_received_ =
      MonotonicTime(std::chrono::nanoseconds(30000000));

  EXPECT_CALL(stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name,
                           "grpc.lyft.users.BadCompanions.GetBadCompanions.upstream_rq_time"),
                  10ul));
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", "application/grpc+proto"},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

} // namespace
} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

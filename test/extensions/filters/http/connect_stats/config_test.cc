#include "envoy/extensions/filters/http/connect_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/connect_stats/v3/config.pb.validate.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/connect_stats/connect_stats_filter.h"

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
namespace ConnectStats {
namespace {

class ConnectStatsFilterConfigTest : public testing::Test {
protected:
  void initialize() {
    ConnectStatsFilterConfigFactory factory;
    auto* service = config_.mutable_individual_method_stats_allowlist()->mutable_services()->Add();
    service->set_name("ServiceA");
    service->mutable_method_names()->Add("Method1");
    Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config_, "stats", context_);
    Http::MockFilterChainFactoryCallbacks filter_callback;

    ON_CALL(filter_callback, addStreamFilter(_)).WillByDefault(testing::SaveArg<0>(&filter_));
    EXPECT_CALL(filter_callback, addStreamFilter(_));
    cb(filter_callback);

    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

    ON_CALL(*decoder_callbacks_.cluster_info_, statsScope())
        .WillByDefault(testing::ReturnRef(stats_store_));

    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  std::string rpcPath(absl::string_view service, absl::string_view method) {
    return fmt::format("/{}/{}", service, method);
  }

  void doStreamingRequest(std::string path) {
    Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/connect+proto"},
                                                   {":path", path}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

    Buffer::OwnedImpl buffer{"{}"};
    Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  }

  envoy::extensions::filters::http::connect_stats::v3::FilterConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::StreamFilterSharedPtr filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

TEST_F(ConnectStatsFilterConfigTest, StatsNormalStreaming) {
  initialize();

  doStreamingRequest(rpcPath("ServiceA", "Method1"));

  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.total").value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.connect_stats"));
}

TEST_F(ConnectStatsFilterConfigTest, StatsErrorStreaming) {
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/connect+proto"},
                                                 {":path", rpcPath("ServiceA", "Method1")}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl buffer{R"({"error":{"code":"unavailable"}})"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, buffer);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceA.Method1.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.failure").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.total").value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.connect_stats"));
}

TEST_F(ConnectStatsFilterConfigTest, StatsNormalUnary) {
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"connect-protocol-version", "1"},
                                                 {"content-type", "application/proto"},
                                                 {":path", rpcPath("ServiceA", "Method1")}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));

  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.total").value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.connect_stats"));
}

TEST_F(ConnectStatsFilterConfigTest, StatsErrorUnary) {
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"connect-protocol-version", "1"},
                                                 {"content-type", "application/proto"},
                                                 {":path", rpcPath("ServiceA", "Method1")}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl buffer{R"({"error":{"code":"unavailable"}})"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceA.Method1.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.failure").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.ServiceA.Method1.total").value());
  EXPECT_FALSE(stream_info_.filterState()->hasDataWithName("envoy.filters.http.connect_stats"));
}

TEST_F(ConnectStatsFilterConfigTest, StatsAllowlistMismatchMethod) {
  initialize();

  doStreamingRequest(rpcPath("ServiceA", "Method2"));

  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceA.Method2.success").value());
  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceA.Method2.total").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.total").value());
}

TEST_F(ConnectStatsFilterConfigTest, StatsAllowlistMismatchService) {
  initialize();

  doStreamingRequest(rpcPath("ServiceB", "Method1"));

  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceB.Method1.success").value());
  EXPECT_EQ(0UL, stats_store_.counterFromString("grpc.ServiceB.Method1.total").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.success").value());
  EXPECT_EQ(1UL, stats_store_.counterFromString("grpc.total").value());
}

TEST_F(ConnectStatsFilterConfigTest, MessageCounts) {
  config_.set_emit_filter_state(true);
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{{"content-type", "application/connect+proto"},
                                                 {":path", rpcPath("ServiceA", "Method1")}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  ProtobufWkt::Value v1;
  v1.set_string_value("v1");
  auto b1 = Grpc::Common::serializeToGrpcFrame(v1);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*b1, false));
  ProtobufWkt::Value v2;
  v2.set_string_value("v2");
  auto b2 = Grpc::Common::serializeToGrpcFrame(v2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*b2, false));

  EXPECT_EQ(2U,
            stats_store_.counterFromString("grpc.ServiceA.Method1.request_message_count").value());

  EXPECT_FALSE(stats_store_.findCounterByString("grpc.ServiceA.Method1.response_message_count"));
  EXPECT_TRUE(stats_store_.findCounterByString("grpc.ServiceA.Method1.request_message_count"));

  const auto* data = stream_info_.filterState()->getDataReadOnly<ConnectStatsObject>(
      "envoy.filters.http.connect_stats");
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(0U, data->response_message_count);

  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "application/connect+proto"},
                                                   {":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add(*b1);
  buffer.add(*b2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  EXPECT_EQ(2U,
            stats_store_.counterFromString("grpc.ServiceA.Method1.request_message_count").value());
  EXPECT_EQ(2U,
            stats_store_.counterFromString("grpc.ServiceA.Method1.response_message_count").value());
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(2U, data->response_message_count);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*b1, true));
  EXPECT_EQ(2U,
            stats_store_.counterFromString("grpc.ServiceA.Method1.request_message_count").value());
  EXPECT_EQ(3U,
            stats_store_.counterFromString("grpc.ServiceA.Method1.response_message_count").value());
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(3U, data->response_message_count);

  // The end-of-stream frame should not count as a response message.
  Buffer::OwnedImpl eos{"{}"};
  Grpc::Encoder().prependFrameHeader(Envoy::Grpc::CONNECT_FH_EOS, eos);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(eos, true));
  EXPECT_EQ(2U, data->request_message_count);
  EXPECT_EQ(3U, data->response_message_count);

  auto filter_object =
      *dynamic_cast<envoy::extensions::filters::http::connect_stats::v3::FilterObject*>(
          data->serializeAsProto().get());
  EXPECT_EQ(2U, filter_object.request_message_count());
  EXPECT_EQ(3U, filter_object.response_message_count());
  EXPECT_EQ("2,3", data->serializeAsString().value());
}

TEST_F(ConnectStatsFilterConfigTest, UpstreamStats) {
  config_.set_emit_filter_state(true);
  config_.set_enable_upstream_stats(true);
  initialize();

  stream_info_.upstream_info_->upstreamTiming().last_upstream_tx_byte_sent_ =
      MonotonicTime(std::chrono::nanoseconds(20000000));
  stream_info_.upstream_info_->upstreamTiming().last_upstream_rx_byte_received_ =
      MonotonicTime(std::chrono::nanoseconds(30000000));

  EXPECT_CALL(stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "grpc.ServiceA.Method1.upstream_rq_time"), 10ul));

  doStreamingRequest(rpcPath("ServiceA", "Method1"));
}

} // namespace
} // namespace ConnectStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

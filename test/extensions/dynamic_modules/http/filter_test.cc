#include <cstring>

#include "source/common/http/message_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

INSTANTIATE_TEST_SUITE_P(LanguageTests, DynamicModuleTestLanguages, testing::Values("c", "rust"),
                         DynamicModuleTestLanguages::languageParamToTestName);

TEST_P(DynamicModuleTestLanguages, Nop) {
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  const auto language = GetParam();
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());
  filter->initializeInModuleFilter();

  // The followings are mostly for coverage at the moment.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(headers, false));
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(trailers));
  MetadataMap metadata;
  EXPECT_EQ(FilterMetadataStatus::Continue, filter->decodeMetadata(metadata));
  filter->decodeComplete();
  TestResponseHeaderMapImpl response_headers{{}};
  EXPECT_EQ(Filter1xxHeadersStatus::Continue, filter->encode1xxHeaders(response_headers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));
  TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));
  EXPECT_EQ(FilterMetadataStatus::Continue, filter->encodeMetadata(metadata));
  filter->encodeComplete();
  filter->onStreamComplete();
  filter->onDestroy();
}

TEST(DynamicModulesTest, ConfigInitializationFailure) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status = newDynamicModuleHttpFilterConfig(
      "config_init_failure", "", false, std::move(dynamic_module.value()),
      *stats_store.createScope(""), context);
  EXPECT_FALSE(filter_config_or_status.ok());
  EXPECT_THAT(filter_config_or_status.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module"));
}

TEST(DynamicModulesTest, StatsCallbacks) {
  const std::string filter_name = "stats_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::TestUtil::TestStore stats_store;
  Stats::TestUtil::TestScope stats_scope{"", stats_store};
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope.symbolTable());
  filter->initializeInModuleFilter();

  Stats::CounterOptConstRef counter =
      stats_store.findCounterByString("dynamicmodulescustom.streams_total");
  EXPECT_TRUE(counter.has_value());
  EXPECT_EQ(counter->get().value(), 1);
  Stats::GaugeOptConstRef gauge =
      stats_store.findGaugeByString("dynamicmodulescustom.concurrent_streams");
  EXPECT_TRUE(gauge.has_value());
  EXPECT_EQ(gauge->get().value(), 1);
  Stats::GaugeOptConstRef magicNumberGauge =
      stats_store.findGaugeByString("dynamicmodulescustom.magic_number");
  EXPECT_TRUE(gauge.has_value());
  EXPECT_EQ(magicNumberGauge->get().value(), 42);
  Stats::HistogramOptConstRef histogram =
      stats_store.findHistogramByString("dynamicmodulescustom.ones");
  EXPECT_TRUE(histogram.has_value());
  EXPECT_FALSE(stats_store.histogramRecordedValues("dynamicmodulescustom.ones"));

  Stats::CounterOptConstRef counter_vec_increment =
      stats_store.findCounterByString("dynamicmodulescustom.test_counter_vec.test_label.increment");
  EXPECT_TRUE(counter_vec_increment.has_value());
  EXPECT_EQ(counter_vec_increment->get().value(), 1);
  Stats::GaugeOptConstRef gauge_vec_increase =
      stats_store.findGaugeByString("dynamicmodulescustom.test_gauge_vec.test_label.increase");
  EXPECT_TRUE(gauge_vec_increase.has_value());
  EXPECT_EQ(gauge_vec_increase->get().value(), 1);
  Stats::GaugeOptConstRef gauge_vec_decrease =
      stats_store.findGaugeByString("dynamicmodulescustom.test_gauge_vec.test_label.decrease");
  EXPECT_TRUE(gauge_vec_decrease.has_value());
  EXPECT_EQ(gauge_vec_decrease->get().value(), 2);
  Stats::GaugeOptConstRef gauge_vec_set =
      stats_store.findGaugeByString("dynamicmodulescustom.test_gauge_vec.test_label.set");
  EXPECT_TRUE(gauge_vec_set.has_value());
  EXPECT_EQ(gauge_vec_set->get().value(), 9001);
  Stats::HistogramOptConstRef histogram_vec_record = stats_store.findHistogramByString(
      "dynamicmodulescustom.test_histogram_vec.test_label.record");
  EXPECT_TRUE(histogram_vec_record.has_value());
  EXPECT_EQ(stats_store.histogramValues("dynamicmodulescustom.test_histogram_vec.test_label.record",
                                        false),
            (std::vector<uint64_t>{1}));

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  std::initializer_list<std::pair<std::string, std::string>> headers = {{"header", "header_value"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  Stats::CounterOptConstRef counter_vec_header = stats_store.findCounterByString(
      "dynamicmodulescustom.test_counter_vec.test_label.header_value");
  EXPECT_EQ(counter_vec_header->get().value(), 1);
  Stats::GaugeOptConstRef gauge_vec_header =
      stats_store.findGaugeByString("dynamicmodulescustom.test_gauge_vec.test_label.header_value");
  EXPECT_EQ(gauge_vec_header->get().value(), 1);
  Stats::HistogramOptConstRef histogram_vec_header = stats_store.findHistogramByString(
      "dynamicmodulescustom.test_histogram_vec.test_label.header_value");
  EXPECT_TRUE(histogram_vec_header.has_value());
  EXPECT_EQ(stats_store.histogramValues(
                "dynamicmodulescustom.test_histogram_vec.test_label.header_value", false),
            (std::vector<uint64_t>{1}));

  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));
  EXPECT_EQ(counter->get().value(), 1);
  EXPECT_EQ(gauge->get().value(), 1);
  EXPECT_EQ(stats_store.histogramValues("dynamicmodulescustom.ones", false),
            (std::vector<uint64_t>{1}));

  filter->onStreamComplete();
  EXPECT_EQ(counter->get().value(), 1);
  EXPECT_EQ(gauge->get().value(), 0);
  EXPECT_EQ(stats_store.histogramValues("dynamicmodulescustom.ones", false),
            (std::vector<uint64_t>{1}));
  Stats::CounterOptConstRef counter_vec_local_var =
      stats_store.findCounterByString("dynamicmodulescustom.test_counter_vec.test_label.local_var");
  EXPECT_EQ(counter_vec_local_var->get().value(), 1);
  Stats::GaugeOptConstRef gauge_vec_local_var =
      stats_store.findGaugeByString("dynamicmodulescustom.test_gauge_vec.test_label.local_var");
  EXPECT_EQ(gauge_vec_local_var->get().value(), 1);
  Stats::HistogramOptConstRef histogram_vec_local_var = stats_store.findHistogramByString(
      "dynamicmodulescustom.test_histogram_vec.test_label.local_var");
  EXPECT_TRUE(histogram_vec_local_var.has_value());
  EXPECT_EQ(stats_store.histogramValues(
                "dynamicmodulescustom.test_histogram_vec.test_label.local_var", false),
            (std::vector<uint64_t>{1}));
  filter->onDestroy();
}

TEST(DynamicModulesTest, HeaderCallbacks) {
  const std::string filter_name = "header_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());
  filter->initializeInModuleFilter();

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  EXPECT_CALL(downstream_callbacks, clearRouteCache());
  EXPECT_CALL(decoder_callbacks, downstreamCallbacks())
      .WillOnce(testing::Return(OptRef(downstream_callbacks)));
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  NiceMock<StreamInfo::MockStreamInfo> info;
  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .WillRepeatedly(testing::ReturnPointee(info.downstream_connection_info_provider_));
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.1.1.1", 1234, false);
  info.downstream_connection_info_provider_->setRemoteAddress(addr);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}, {"to-be-deleted", "value"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  filter->onDestroy();
}

TEST(DynamicModulesTest, DynamicMetadataCallbacks) {
  const std::string filter_name = "dynamic_metadata_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();

  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));

  EXPECT_CALL(stream_info, route()).WillRepeatedly(Return(route));
  EXPECT_CALL(callbacks, clusterInfo()).WillRepeatedly(testing::Return(callbacks.cluster_info_));

  Envoy::Config::Metadata::mutableMetadataValue(callbacks.cluster_info_->metadata_, "metadata",
                                                "cluster_key")
      .set_string_value("cluster");
  Envoy::Config::Metadata::mutableMetadataValue(route->metadata_, "metadata", "route_key")
      .set_string_value("route");

  auto upstream_info = std::make_shared<NiceMock<StreamInfo::MockUpstreamInfo>>();
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto host_metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  EXPECT_CALL(*upstream_host, metadata()).WillRepeatedly(testing::Return(host_metadata));
  EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(testing::Return(upstream_info));

  upstream_info->upstream_host_ = upstream_host;
  Envoy::Config::Metadata::mutableMetadataValue(*host_metadata, "metadata", "host_key")
      .set_string_value("host");
  filter->setDecoderFilterCallbacks(callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};
  Http::TestResponseHeaderMapImpl response_headers{};
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));

  // Check dynamic metadata set by the filter during even hooks.
  auto ns_req_header = metadata.filter_metadata().find("ns_req_header");
  ASSERT_NE(ns_req_header, metadata.filter_metadata().end());
  auto key = ns_req_header->second.fields().find("key");
  ASSERT_NE(key, ns_req_header->second.fields().end());
  EXPECT_EQ(key->second.number_value(), 123);
  auto ns_res_header = metadata.filter_metadata().find("ns_res_header");
  ASSERT_NE(ns_res_header, metadata.filter_metadata().end());
  key = ns_res_header->second.fields().find("key");
  ASSERT_NE(key, ns_res_header->second.fields().end());
  EXPECT_EQ(key->second.number_value(), 123);
  auto ns_req_body = metadata.filter_metadata().find("ns_req_body");
  ASSERT_NE(ns_req_body, metadata.filter_metadata().end());
  key = ns_req_body->second.fields().find("key");
  ASSERT_NE(key, ns_req_body->second.fields().end());
  EXPECT_EQ(key->second.string_value(), "value");
  auto ns_res_body = metadata.filter_metadata().find("ns_res_body");
  ASSERT_NE(ns_res_body, metadata.filter_metadata().end());
  key = ns_res_body->second.fields().find("key");
  ASSERT_NE(key, ns_res_body->second.fields().end());
  EXPECT_EQ(key->second.string_value(), "value");

  filter->onDestroy();
}

TEST(DynamicModulesTest, FilterStateCallbacks) {
  const std::string filter_name = "filter_state_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, filterState())
      .WillRepeatedly(testing::ReturnRef(stream_info.filter_state_));
  filter->setDecoderFilterCallbacks(callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};
  Http::TestRequestTrailerMapImpl request_trailers{};
  Http::TestResponseHeaderMapImpl response_headers{};
  Http::TestResponseTrailerMapImpl response_trailers{};
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  // Check filter state set by the filter during even hooks.
  const auto* req_header_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_header_key");
  ASSERT_NE(req_header_value, nullptr);
  EXPECT_EQ(req_header_value->serializeAsString(), "req_header_value");
  const auto* req_body_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_body_key");
  ASSERT_NE(req_body_value, nullptr);
  EXPECT_EQ(req_body_value->serializeAsString(), "req_body_value");
  const auto* req_trailer_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_trailer_key");
  ASSERT_NE(req_trailer_value, nullptr);
  EXPECT_EQ(req_trailer_value->serializeAsString(), "req_trailer_value");
  const auto* res_header_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_header_key");
  ASSERT_NE(res_header_value, nullptr);
  EXPECT_EQ(res_header_value->serializeAsString(), "res_header_value");
  const auto* res_body_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_body_key");
  ASSERT_NE(res_body_value, nullptr);
  EXPECT_EQ(res_body_value->serializeAsString(), "res_body_value");
  const auto* res_trailer_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_trailer_key");
  ASSERT_NE(res_trailer_value, nullptr);
  EXPECT_EQ(res_trailer_value->serializeAsString(), "res_trailer_value");
  // There is no filter state named key set by the filter.
  const auto* value = stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("key");
  ASSERT_EQ(value, nullptr);

  filter->onStreamComplete();
  const auto* stream_complete_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("stream_complete_key");
  ASSERT_NE(stream_complete_value, nullptr);
  EXPECT_EQ(stream_complete_value->serializeAsString(), "stream_complete_value");
}

TEST(DynamicModulesTest, BodyCallbacks) {
  const std::string filter_name = "body_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  Buffer::OwnedImpl request_body;
  EXPECT_CALL(decoder_callbacks, decodingBuffer()).WillRepeatedly(testing::Return(&request_body));
  EXPECT_CALL(decoder_callbacks, addDecodedData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {}));
  Buffer::OwnedImpl response_body;
  EXPECT_CALL(encoder_callbacks, encodingBuffer()).WillRepeatedly(testing::Return(&response_body));
  EXPECT_CALL(encoder_callbacks, addEncodedData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {}));
  EXPECT_CALL(decoder_callbacks, modifyDecodingBuffer(_))
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(request_body);
      }));
  EXPECT_CALL(encoder_callbacks, modifyEncodingBuffer(_))
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(response_body);
      }));

  request_body.add("nice");
  filter->decodeData(request_body, false);
  EXPECT_EQ(request_body.toString(), "foo");
  request_body.drain(request_body.length());
  request_body.add("nice");
  filter->decodeData(request_body, false);
  EXPECT_EQ(request_body.toString(), "foo");
  request_body.drain(request_body.length());
  request_body.add("nice");
  filter->decodeData(request_body, true);
  EXPECT_EQ(request_body.toString(), "fooend");

  response_body.add("cool");
  filter->encodeData(response_body, false);
  EXPECT_EQ(response_body.toString(), "bar");
  response_body.drain(response_body.length());
  response_body.add("cool");
  filter->encodeData(response_body, false);
  EXPECT_EQ(response_body.toString(), "bar");
  response_body.drain(response_body.length());
  response_body.add("cool");
  filter->encodeData(response_body, true);
  EXPECT_EQ(response_body.toString(), "barend");
}

TEST(DynamicModulesTest, HttpFilterHttpCalloutNonExistingCluster) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "non_existent_cluster";
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(absl::string_view{filter_config}))
      .WillOnce(testing::Return(nullptr));
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(callbacks, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(callbacks, encodeHeaders_(_, true));

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));
}

TEST(DynamicModulesTest, HttpFilterHttpCalloutImmediateFailingCluster) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "immediate_failing_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(absl::string_view{filter_config}))
      .WillOnce(testing::Return(cluster.get()));

  EXPECT_CALL(cluster->async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            testing::NiceMock<Http::MockAsyncClientRequest> req{
                &cluster->async_client_}; // This is not used, just for making compiler happy.
            // Simulate immediate failure where onFailure is called inline.
            callbacks.onFailure(req, Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(callbacks, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(callbacks, encodeHeaders_(_, true));

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));
}

TEST(DynamicModulesTest, HttpFilterHttpCalloutSuccess) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "success_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(absl::string_view{filter_config}))
      .WillRepeatedly(testing::Return(cluster.get()));

  NiceMock<Http::MockAsyncClientRequest> request(&cluster->async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(cluster->async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions& option) -> Http::AsyncClient::Request* {
            EXPECT_EQ(message->headers().Path()->value().getStringView(), "/");
            EXPECT_EQ(message->headers().Method()->value().getStringView(), "GET");
            EXPECT_EQ(message->headers().Host()->value().getStringView(), "example.com");
            EXPECT_EQ(message->body().toString(), "http_callout_body");
            EXPECT_EQ(option.timeout.value(), std::chrono::milliseconds(1000));
            callbacks_captured = &callbacks;
            return &request;
          }));

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(callbacks, sendLocalReply(Http::Code::OK, _, _, _, _));
  EXPECT_CALL(callbacks, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks, encodeData(_, true));

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));

  testing::NiceMock<Http::MockAsyncClientRequest> req{
      &cluster->async_client_}; // This is not used, just for making compiler happy.
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {"some_header", "some_value"},
  }));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add("response_body_from_callout");

  EXPECT_TRUE(callbacks_captured);
  callbacks_captured->onSuccess(req, std::move(response));
}

TEST(DynamicModulesTest, HttpFilterHttpCalloutResetting) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "resetting_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(absl::string_view{filter_config}))
      .WillRepeatedly(testing::Return(cluster.get()));

  NiceMock<Http::MockAsyncClientRequest> request(&cluster->async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(cluster->async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));

  testing::NiceMock<Http::MockAsyncClientRequest> req{
      &cluster->async_client_}; // This is not used, just for making compiler happy.
  EXPECT_TRUE(callbacks_captured);
  callbacks_captured->onFailure(req, Http::AsyncClient::FailureReason::Reset);
}

// This test verifies that handling of per-route config is correct in terms of lifetimes.
TEST(DynamicModulesTest, HttpFilterPerFilterConfigLifetimes) {
  const std::string filter_name = "per_route_config";
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "listener config";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()), *stats_scope,
          context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto dynamic_module_for_route =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module_for_route.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  filter->initializeInModuleFilter();

  // Now simulate a per-route config that is very short lived, and verify that the filter doesn't
  // segfaults if it uses it after after it discarded.
  {
    // do all per-route config in an inner scope to make sure the per-route config is destroyed
    // before the filter response headers is called.
    const std::string route_filter_config_str = "router config";
    auto route_filter_config_or_status =
        Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
            filter_name, route_filter_config_str, std::move(dynamic_module_for_route.value()),
            false);
    EXPECT_TRUE(route_filter_config_or_status.ok());
    auto route_filter_config = std::move(route_filter_config_or_status.value());

    const Router::RouteSpecificFilterConfig* router_config_ptr = route_filter_config.get();

    // The filter checks for disabled flag first, then the module also requests per-route config.
    auto route = std::make_shared<NiceMock<Router::MockRoute>>();
    ON_CALL(*route, mostSpecificPerFilterConfig(_))
        .WillByDefault(testing::Return(router_config_ptr));
    ON_CALL(decoder_callbacks, route()).WillByDefault(testing::Return(route));

    TestRequestHeaderMapImpl headers{{}};
    EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(headers, true));
    route_filter_config.reset();
  }

  TestResponseHeaderMapImpl response_headers{{}};
  EXPECT_CALL(encoder_callbacks, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, true));

  // Assert response header is what we expect
  EXPECT_EQ(response_headers.get(Http::LowerCaseString("x-per-route-config-response"))[0]
                ->value()
                .getStringView(),
            "router config");
}

TEST(HttpFilter, HeaderMapGetter) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter(nullptr, symbol_table);

  EXPECT_EQ(absl::nullopt, filter.requestHeaders());
  EXPECT_EQ(absl::nullopt, filter.requestTrailers());
  EXPECT_EQ(absl::nullopt, filter.responseHeaders());
  EXPECT_EQ(absl::nullopt, filter.responseTrailers());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter.setDecoderFilterCallbacks(decoder_callbacks);
  filter.setEncoderFilterCallbacks(encoder_callbacks);

  EXPECT_CALL(decoder_callbacks, requestHeaders()).WillOnce(testing::Return(absl::nullopt));
  EXPECT_CALL(decoder_callbacks, requestTrailers()).WillOnce(testing::Return(absl::nullopt));
  EXPECT_CALL(encoder_callbacks, responseHeaders()).WillOnce(testing::Return(absl::nullopt));
  EXPECT_CALL(encoder_callbacks, responseTrailers()).WillOnce(testing::Return(absl::nullopt));

  EXPECT_EQ(absl::nullopt, filter.requestHeaders());
  EXPECT_EQ(absl::nullopt, filter.requestTrailers());
  EXPECT_EQ(absl::nullopt, filter.responseHeaders());
  EXPECT_EQ(absl::nullopt, filter.responseTrailers());

  TestRequestHeaderMapImpl request_headers{{}};
  TestResponseHeaderMapImpl response_headers{{}};
  TestRequestTrailerMapImpl request_trailers{{}};
  TestResponseTrailerMapImpl response_trailers{{}};
  EXPECT_CALL(decoder_callbacks, requestHeaders())
      .WillOnce(testing::Return(makeOptRef<Http::RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks, requestTrailers())
      .WillOnce(testing::Return(makeOptRef<Http::RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks, responseHeaders())
      .WillOnce(testing::Return(makeOptRef<Http::ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks, responseTrailers())
      .WillOnce(testing::Return(makeOptRef<Http::ResponseTrailerMap>(response_trailers)));
  EXPECT_EQ(request_headers, filter.requestHeaders().value());
  EXPECT_EQ(request_trailers, filter.requestTrailers().value());
  EXPECT_EQ(response_headers, filter.responseHeaders().value());
  EXPECT_EQ(response_trailers, filter.responseTrailers().value());
}

// Test sendStreamData on invalid stream handle returns false.
TEST(HttpFilter, SendStreamDataOnInvalidStream) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter(nullptr, symbol_table);

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  // Try to send data on a non-existent stream (invalid handle).
  Buffer::OwnedImpl data("test");
  EXPECT_FALSE(filter.sendStreamData(0, data, false));
}

// Test resetHttpStream on invalid stream handle.
TEST(HttpFilter, ResetInvalidStream) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter(nullptr, symbol_table);

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  // Try to reset a non-existent stream (invalid handle).
  filter.resetHttpStream(0);
  // Should not crash - just no-op for invalid handle.
}

// Test sendStreamTrailers on invalid stream handle.
TEST(HttpFilter, SendStreamTrailersOnInvalidStream) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter(nullptr, symbol_table);

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter.setDecoderFilterCallbacks(decoder_callbacks);

  // Try to send trailers on a non-existent stream (invalid handle).
  auto trailers = Http::RequestTrailerMapImpl::create();
  EXPECT_FALSE(filter.sendStreamTrailers(0, std::move(trailers)));
}

TEST(DynamicModulesTest, HttpFilterHttpStreamCalloutOnComplete) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");

  Upstream::MockClusterManager cluster_manager;
  auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(cluster.get()));

  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          "filter", "", false, std::move(dynamic_module.value()), *stats_scope, context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  // Mock AsyncClient
  NiceMock<Http::MockAsyncClientStream> stream;
  Http::AsyncClient::StreamCallbacks* captured_callbacks = nullptr;
  EXPECT_CALL(cluster->async_client_, start(_, _))
      .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions&) -> Http::AsyncClient::Stream* {
        captured_callbacks = &callbacks;
        return &stream;
      }));

  // Start Stream
  uint64_t stream_id;
  auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}});
  auto message = std::make_unique<Http::RequestMessageImpl>(std::move(headers));
  auto result = filter->startHttpStream(&stream_id, "cluster", std::move(message), false, 1000);
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(captured_callbacks, nullptr);

  // Invoke onComplete
  if (captured_callbacks) {
    captured_callbacks->onComplete();
  }
}

TEST(DynamicModulesTest, StartHttpStreamDoesNotSetContentLength) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");

  Upstream::MockClusterManager cluster_manager;
  auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(cluster.get()));

  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          "filter", "", false, std::move(dynamic_module.value()), *stats_scope, context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  // Mock AsyncClient
  NiceMock<Http::MockAsyncClientStream> stream;
  Http::AsyncClient::StreamCallbacks* captured_callbacks = nullptr;
  EXPECT_CALL(cluster->async_client_, start(_, _))
      .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions&) -> Http::AsyncClient::Stream* {
        captured_callbacks = &callbacks;
        return &stream;
      }));

  EXPECT_CALL(stream, sendHeaders(_, false))
      .WillOnce(Invoke([](Http::RequestHeaderMap& headers, bool) {
        EXPECT_EQ(nullptr, headers.ContentLength());
      }));
  EXPECT_CALL(stream, sendData(_, true));

  uint64_t stream_id = 0;
  char cluster_name[] = "cluster";
  char method_key[] = ":method";
  char method_value[] = "POST";
  char path_key[] = ":path";
  char path_value[] = "/";
  char authority_key[] = ":authority";
  char authority_value[] = "host";
  envoy_dynamic_module_type_module_http_header headers[] = {
      {method_key, strlen(method_key), method_value, strlen(method_value)},
      {path_key, strlen(path_key), path_value, strlen(path_value)},
      {authority_key, strlen(authority_key), authority_value, strlen(authority_value)}};
  char body[] = "hello";

  auto result = envoy_dynamic_module_callback_http_filter_start_http_stream(
      filter.get(), &stream_id, cluster_name, strlen(cluster_name), headers, 3, body,
      sizeof(body) - 1, true, 1000);
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(captured_callbacks, nullptr);
  EXPECT_NE(stream_id, 0);
  if (captured_callbacks != nullptr) {
    captured_callbacks->onComplete();
  }
}

TEST(DynamicModulesTest, StartHttpStreamHandlesInlineResetDuringHeaders) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");

  Upstream::MockClusterManager cluster_manager;
  auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(cluster.get()));

  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          "filter", "", false, std::move(dynamic_module.value()), *stats_scope, context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  // Mock AsyncClient
  NiceMock<Http::MockAsyncClientStream> stream;
  Http::AsyncClient::StreamCallbacks* captured_callbacks = nullptr;
  EXPECT_CALL(cluster->async_client_, start(_, _))
      .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions&) -> Http::AsyncClient::Stream* {
        captured_callbacks = &callbacks;
        return &stream;
      }));

  EXPECT_CALL(stream, sendHeaders(_, false)).WillOnce(Invoke([&](Http::RequestHeaderMap&, bool) {
    ASSERT_NE(captured_callbacks, nullptr);
    captured_callbacks->onReset();
  }));
  EXPECT_CALL(stream, sendData(_, _)).Times(0);

  uint64_t stream_id;
  auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":method", "POST"}, {":path", "/"}, {":authority", "host"}});
  auto message = std::make_unique<Http::RequestMessageImpl>(std::move(headers));
  message->body().add(absl::string_view("payload"));

  auto result = filter->startHttpStream(&stream_id, "cluster", std::move(message),
                                        true /* end_stream */, 1000);
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(captured_callbacks, nullptr);
}

TEST(DynamicModulesTest, HttpStreamCalloutDeferredDeleteOnDestroy) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");

  Upstream::MockClusterManager cluster_manager;
  auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(cluster.get()));

  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          "filter", "", false, std::move(dynamic_module.value()), *stats_scope, context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();

  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, dispatcher()).WillRepeatedly(testing::ReturnRef(dispatcher));
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  // Mock AsyncClient
  NiceMock<Http::MockAsyncClientStream> stream;
  Http::AsyncClient::StreamCallbacks* captured_callbacks = nullptr;
  EXPECT_CALL(cluster->async_client_, start(_, _))
      .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions&) -> Http::AsyncClient::Stream* {
        captured_callbacks = &callbacks;
        return &stream;
      }));

  uint64_t stream_id;
  auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}});
  auto message = std::make_unique<Http::RequestMessageImpl>(std::move(headers));
  auto result = filter->startHttpStream(&stream_id, "cluster", std::move(message), false, 1000);
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(captured_callbacks, nullptr);

  EXPECT_CALL(dispatcher, deferredDelete_(_));
  filter->onDestroy();

  // Upstream callbacks may still run after destroy; ensure they safely return.
  captured_callbacks->onComplete();

  dispatcher.clearDeferredDeleteList();
}

TEST(DynamicModulesTest, HttpFilterHttpStreamCalloutOnReset) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto stats_scope = stats_store.createScope("");

  Upstream::MockClusterManager cluster_manager;
  auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(cluster.get()));

  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          "filter", "", false, std::move(dynamic_module.value()), *stats_scope, context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_scope->symbolTable());
  filter->initializeInModuleFilter();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  // Mock AsyncClient
  NiceMock<Http::MockAsyncClientStream> stream;
  Http::AsyncClient::StreamCallbacks* captured_callbacks = nullptr;
  EXPECT_CALL(cluster->async_client_, start(_, _))
      .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions&) -> Http::AsyncClient::Stream* {
        captured_callbacks = &callbacks;
        return &stream;
      }));

  // Start Stream
  uint64_t stream_id;
  auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":method", "GET"}, {":path", "/"}, {":authority", "host"}});
  auto message = std::make_unique<Http::RequestMessageImpl>(std::move(headers));
  auto result = filter->startHttpStream(&stream_id, "cluster", std::move(message), false, 1000);
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(captured_callbacks, nullptr);

  // Invoke onReset
  if (captured_callbacks) {
    captured_callbacks->onReset();
  }
}

TEST(DynamicModulesTest, PerRouteConfigDisabled) {
  // Test per-route disabled feature. We use C module only because when the filter is disabled,
  // the module is never initialized, and the Rust module has strict lifecycle assertions.
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  // Create a per-route config with disabled=true.
  auto dynamic_module_for_per_route = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module_for_per_route.ok());
  auto per_route_config = newDynamicModuleHttpPerRouteConfig(
      "per_route", "config", std::move(dynamic_module_for_per_route.value()), true);
  EXPECT_TRUE(per_route_config.ok());

  // Mock the route to return the disabled per-route config.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  ON_CALL(*route, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(per_route_config.value().get()));
  ON_CALL(decoder_callbacks, route()).WillByDefault(testing::Return(route));

  TestRequestHeaderMapImpl request_headers{{}};
  // decodeHeaders should return Continue and not initialize the in-module filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  // The filter should be disabled, so no module interaction should occur.

  // All subsequent decode callbacks should return Continue without calling the module.
  Buffer::OwnedImpl request_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(request_data, false));
  TestRequestTrailerMapImpl request_trailers{{}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));

  // All encode callbacks should also return Continue without calling the module.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl response_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(response_data, false));
  TestResponseTrailerMapImpl response_trailers{{}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  filter->onDestroy();
}

TEST(DynamicModulesTest, PerRouteConfigDisabledWithBuffering) {
  // Test per-route disabled with buffering enabled. We use C module only because when the
  // filter is disabled, the module is never initialized, and the Rust module has strict
  // lifecycle assertions.
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  // Create a per-route config with disabled=true.
  auto dynamic_module_for_per_route = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module_for_per_route.ok());
  auto per_route_config = newDynamicModuleHttpPerRouteConfig(
      "per_route", "config", std::move(dynamic_module_for_per_route.value()), true);
  EXPECT_TRUE(per_route_config.ok());

  // Mock the route to return the disabled per-route config.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  ON_CALL(*route, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(per_route_config.value().get()));
  ON_CALL(decoder_callbacks, route()).WillByDefault(testing::Return(route));

  TestRequestHeaderMapImpl request_headers{{}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));

  // Test with buffering enabled - should handle end_stream correctly.
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks, decodingBuffer())
      .WillRepeatedly(testing::Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks, addDecodedData(_, false));

  Buffer::OwnedImpl request_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(request_data, true));

  // Test encode path with buffering.
  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks, encodingBuffer())
      .WillRepeatedly(testing::Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks, addEncodedData(_, false));

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl response_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(response_data, true));

  filter->onDestroy();
}

TEST_P(DynamicModuleTestLanguages, PerRouteConfigDisabledImmediateResponse) {
  // Test the scenario where a previous filter sends an immediate response and encodeHeaders
  // is called without decodeHeaders being called first.
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  const auto language = GetParam();
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());

  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  // Create a per-route config with disabled=true.
  auto dynamic_module_for_per_route =
      newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(dynamic_module_for_per_route.ok());
  auto per_route_config = newDynamicModuleHttpPerRouteConfig(
      "per_route", "config", std::move(dynamic_module_for_per_route.value()), true);
  EXPECT_TRUE(per_route_config.ok());

  // Mock the route to return the disabled per-route config.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  ON_CALL(*route, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(per_route_config.value().get()));
  ON_CALL(encoder_callbacks, route()).WillByDefault(testing::Return(route));

  // Directly call encodeHeaders without calling decodeHeaders first.
  // This simulates an immediate response scenario.
  TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));

  // All subsequent encode callbacks should return Continue without calling the module.
  Buffer::OwnedImpl response_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(response_data, false));
  TestResponseTrailerMapImpl response_trailers{{}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  filter->onDestroy();
}

TEST(DynamicModulesTest, ImmediateResponseFilterEnabled) {
  // Test the scenario where a previous filter sends an immediate response and encodeHeaders
  // is called without decodeHeaders being called first, but the filter is not disabled.
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Stats::IsolatedStoreImpl stats_store;
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, false, std::move(dynamic_module.value()),
          *stats_store.createScope(""), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value(),
                                                          stats_store.symbolTable());

  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);

  // No per-route config set, so the filter should be enabled.
  ON_CALL(encoder_callbacks, route()).WillByDefault(testing::Return(nullptr));

  // Directly call encodeHeaders without calling decodeHeaders first.
  // The module should be initialized in encodeHeaders and the callback should be invoked.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));

  // Verify subsequent callbacks work correctly.
  Buffer::OwnedImpl response_data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(response_data, false));
  TestResponseTrailerMapImpl response_trailers{{}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  filter->onDestroy();
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy

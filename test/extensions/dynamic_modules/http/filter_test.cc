#include "source/common/http/message_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
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
  auto filter_config_or_status = newDynamicModuleHttpFilterConfig(
      "config_init_failure", "", std::move(dynamic_module.value()), context);
  EXPECT_FALSE(filter_config_or_status.ok());
  EXPECT_THAT(filter_config_or_status.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module"));
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
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  EXPECT_CALL(downstream_callbacks, clearRouteCache());
  EXPECT_CALL(decoder_callbacks, downstreamCallbacks())
      .WillOnce(testing::Return(OptRef(downstream_callbacks)));
  filter->setDecoderFilterCallbacks(decoder_callbacks);

  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
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
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  Http::MockStreamDecoderFilterCallbacks callbacks;
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
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  Http::MockStreamDecoderFilterCallbacks callbacks;
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
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
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

TEST(DynamicModulesTest, HttpFilterHttpCallout_non_existing_cluster) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
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
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  Http::MockStreamDecoderFilterCallbacks callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();
  filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(callbacks, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(callbacks, encodeHeaders_(_, true));

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));
}

TEST(DynamicModulesTest, HttpFilterHttpCallout_immediate_failing_cluster) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "immediate_failing_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
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

  Http::MockStreamDecoderFilterCallbacks callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();
  filter->setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(callbacks, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(callbacks, encodeHeaders_(_, true));

  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter->decodeHeaders(headers, false));
}

TEST(DynamicModulesTest, HttpFilterHttpCallout_success) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "success_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
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

  Http::MockStreamDecoderFilterCallbacks callbacks;
  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
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

TEST(DynamicModulesTest, HttpFilterHttpCallout_resetting) {
  const std::string filter_name = "http_callouts";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "resetting_cluster";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
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

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

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
  Upstream::MockClusterManager cluster_manager;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster(_))
      .WillRepeatedly(testing::Return(&thread_local_cluster));
  EXPECT_CALL(context, clusterManager()).WillRepeatedly(testing::ReturnRef(cluster_manager));

  const std::string filter_config = "listener config";
  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()), context);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto dynamic_module_for_route =
      newDynamicModule(testSharedObjectPath("http_integration_test", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module_for_route.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;

  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  filter->initializeInModuleFilter();

  // Now simulate a per-route config that is very short lived, and verify that the filter doesn't
  // segfaults if it uses it after after it discarded.
  {
    // do all per-route config in an inner scope to make sure the is destroyed before the filter
    // response headers is called.
    const std::string route_filter_config_str = "router config";
    auto route_filter_config_or_status =
        Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
            filter_name, route_filter_config_str, std::move(dynamic_module_for_route.value()));
    EXPECT_TRUE(route_filter_config_or_status.ok());
    auto route_filter_config = std::move(route_filter_config_or_status.value());

    const Router::RouteSpecificFilterConfig* router_config_ptr = route_filter_config.get();

    EXPECT_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
        .WillOnce(testing::Return(router_config_ptr));

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
  DynamicModuleHttpFilter filter(nullptr);

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

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy

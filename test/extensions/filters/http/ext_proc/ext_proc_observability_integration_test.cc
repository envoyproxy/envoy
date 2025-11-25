#include <algorithm>
#include <iostream>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/json/json_loader.h"
#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using Envoy::Extensions::HttpFilters::ExternalProcessing::verifyMultipleHeaderValues;
using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::ProtocolConfiguration;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::makeHeaderValue;
using Extensions::HttpFilters::ExternalProcessing::OnProcessingResponseFactory;
using Extensions::HttpFilters::ExternalProcessing::TestOnProcessingResponseFactory;
using Http::LowerCaseString;
using test::integration::filters::LoggingTestFilterConfig;
using testing::_;
using testing::Not;

using namespace std::chrono_literals;

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing, ExtProcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtProcIntegrationTest, SendAndReceiveDynamicMetadataObservabilityMode) {
  proto_config_.set_observability_mode(true);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  auto* md_opts = proto_config_.mutable_metadata_options();
  md_opts->mutable_forwarding_namespaces()->add_untyped("forwarding_ns_untyped");
  md_opts->mutable_forwarding_namespaces()->add_typed("forwarding_ns_typed");
  md_opts->mutable_receiving_namespaces()->add_untyped("receiving_ns_untyped");

  ConfigOptions config_option = {};
  config_option.add_metadata = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  testSendDyanmicMetadata();

  handleUpstreamRequest();

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // No headers from dynamic metadata response as the response is ignored in observability mode.
  EXPECT_THAT(response->headers(), Not(ContainsHeader("receiving_ns_untyped.foo", _)));
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithHeader) {
  proto_config_.set_observability_mode(true);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                          {":method", "GET"},
                                                          {"host", "host"},
                                                          {":path", "/"},
                                                          {"x-forwarded-proto", "http"}};
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true,
      [&expected_request_headers](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        // Verify the header request.
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        EXPECT_TRUE(headers.end_of_stream());

        // Try to mutate the header.
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_append()->set_value(false);
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Header mutation response has been ignored.
  EXPECT_THAT(upstream_request_->headers(), Not(ContainsHeader("x-remove-this", _)));

  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(100, true);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });

  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithBody) {
  proto_config_.set_observability_mode(true);

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);

  initializeConfig();
  HttpIntegrationTest::initialize();
  const std::string original_body_str = "Hello";
  auto response = sendDownstreamRequestWithBody(original_body_str, absl::nullopt);

  processRequestBodyMessage(*grpc_upstreams_[0], true,
                            [&original_body_str](const HttpBody& body, BodyResponse& body_resp) {
                              // Verify the received body message.
                              EXPECT_EQ(body.body(), original_body_str);
                              EXPECT_TRUE(body.end_of_stream());
                              // Try to mutate the body.
                              auto* body_mut =
                                  body_resp.mutable_response()->mutable_body_mutation();
                              body_mut->set_body("Hello, World!");
                              return true;
                            });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Body mutation response has been ignored.
  EXPECT_EQ(upstream_request_->body().toString(), original_body_str);

  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(100, true);

  processResponseBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    return true;
  });

  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithWrongBodyMode) {
  proto_config_.set_observability_mode(true);

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();
  const std::string original_body_str = "Hello";
  auto response = sendDownstreamRequestWithBody(original_body_str, absl::nullopt);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithTrailer) {
  proto_config_.set_observability_mode(true);

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  handleUpstreamRequestWithTrailer();

  processResponseTrailersMessage(
      *grpc_upstreams_[0], true, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        // Verify the trailer
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));

        // Try to mutate the trailer
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_append()->set_value(false);
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(*(response->trailers()), Not(ContainsHeader("x-modified-trailers", _)));

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithFullRequest) {
  proto_config_.set_observability_mode(true);
  uint32_t deferred_close_timeout_ms = 1000;
  proto_config_.mutable_deferred_close_timeout()->set_seconds(deferred_close_timeout_ms / 1000);

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();
  const std::string body_str = "Hello";
  auto response = sendDownstreamRequestWithBodyAndTrailer(body_str);

  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processRequestTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(deferred_close_timeout_ms));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithFullResponse) {
  proto_config_.set_observability_mode(true);
  uint32_t deferred_close_timeout_ms = 1000;
  proto_config_.mutable_deferred_close_timeout()->set_seconds(deferred_close_timeout_ms / 1000);

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  handleUpstreamRequestWithTrailer();

  processResponseHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processResponseBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(deferred_close_timeout_ms));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithFullRequestAndTimeout) {
  proto_config_.set_observability_mode(true);
  uint32_t deferred_close_timeout_ms = 2000;
  proto_config_.mutable_deferred_close_timeout()->set_seconds(deferred_close_timeout_ms / 1000);

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBodyAndTrailer("Hello");

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Advance 400 ms. Default timeout is 200ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });
  processRequestBodyMessage(*grpc_upstreams_[0], false, [this](const HttpBody&, BodyResponse&) {
    // Advance 400 ms. Default timeout is 200ms
    timeSystem().advanceTimeWaitImpl(400ms);
    return false;
  });
  processRequestTrailersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpTrailers&, TrailersResponse&) {
                                  // Advance 400 ms. Default timeout is 200ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(deferred_close_timeout_ms - 1200));
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithLogging) {
  proto_config_.set_observability_mode(true);

  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithLoggingFailStream) {
  proto_config_.set_observability_mode(true);

  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  testGetAndFailStream();
}

TEST_P(ExtProcIntegrationTest, ObservabilityModeWithLoggingCloseStream) {
  proto_config_.set_observability_mode(true);

  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  testGetAndCloseStream();
}

TEST_P(ExtProcIntegrationTest, GetAndSetHeadersUpstreamObservabilityMode) {
  proto_config_.set_observability_mode(true);

  ConfigOptions config_option = {};
  config_option.filter_setup = ConfigOptions::FilterSetup::kNone;

  initializeConfig(config_option);
  // Add ext_proc as upstream filter.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    // Retrieve cluster_0.
    auto* cluster = static_resources->mutable_clusters(0);
    ConfigHelper::HttpProtocolOptions old_protocol_options;
    if (cluster->typed_extension_protocol_options().contains(
            "envoy.extensions.upstreams.http.v3.HttpProtocolOptions")) {
      old_protocol_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
          (*cluster->mutable_typed_extension_protocol_options())
              ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
    }
    if (old_protocol_options.http_filters().empty()) {
      auto* http_filter = old_protocol_options.add_http_filters();
      http_filter->set_name("envoy.filters.http.upstream_codec");
      http_filter->mutable_typed_config()->PackFrom(
          envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance());
    }
    auto* ext_proc_filter = old_protocol_options.add_http_filters();
    ext_proc_filter->set_name("envoy.filters.http.ext_proc");
    ext_proc_filter->mutable_typed_config()->PackFrom(proto_config_);
    for (int i = old_protocol_options.http_filters_size() - 1; i > 0; --i) {
      old_protocol_options.mutable_http_filters()->SwapElements(i, i - 1);
    }
    (*cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(old_protocol_options);
  });
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {":authority", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_append()->set_value(false);
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  verifyDownstreamResponse(*response, 200);
}

// Upstream filter chain is in alpha mode and it is not actively used in ext_proc at the moment.
TEST_P(ExtProcIntegrationTest, DISABLED_GetAndSetHeadersUpstreamObservabilityModeWithLogging) {
  proto_config_.set_observability_mode(true);

  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  config_option.filter_setup = ConfigOptions::FilterSetup::kNone;

  initializeConfig(config_option);
  // Add ext_proc as upstream filter.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    // Retrieve cluster_0.
    auto* cluster = static_resources->mutable_clusters(0);
    ConfigHelper::HttpProtocolOptions old_protocol_options;
    if (cluster->typed_extension_protocol_options().contains(
            "envoy.extensions.upstreams.http.v3.HttpProtocolOptions")) {
      old_protocol_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
          (*cluster->mutable_typed_extension_protocol_options())
              ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
    }
    if (old_protocol_options.http_filters().empty()) {
      auto* http_filter = old_protocol_options.add_http_filters();
      http_filter->set_name("envoy.filters.http.upstream_codec");
      http_filter->mutable_typed_config()->PackFrom(
          envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance());
    }
    auto* ext_proc_filter = old_protocol_options.add_http_filters();
    ext_proc_filter->set_name("envoy.filters.http.ext_proc");
    ext_proc_filter->mutable_typed_config()->PackFrom(proto_config_);
    for (int i = old_protocol_options.http_filters_size() - 1; i > 0; --i) {
      old_protocol_options.mutable_http_filters()->SwapElements(i, i - 1);
    }
    (*cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(old_protocol_options);
  });
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {":authority", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_append()->set_value(false);
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, InvalidServerOnResponseInObservabilityMode) {
  proto_config_.set_observability_mode(true);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);

  ConfigOptions config_option = {};
  config_option.valid_grpc_server = false;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  handleUpstreamRequest();
  EXPECT_FALSE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_,
                                                         std::chrono::milliseconds(25000)));
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

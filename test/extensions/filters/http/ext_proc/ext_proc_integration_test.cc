#include <algorithm>
#include <iostream>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::makeHeaderValue;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

using Http::LowerCaseString;

using namespace std::chrono_literals;

struct ConfigOptions {
  bool valid_grpc_server = true;
  bool add_logging_filter = false;
  bool http1_codec = false;
  bool add_metadata = false;
  bool downstream_filter = true;
};

// These tests exercise the ext_proc filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class ExtProcIntegrationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
protected:
  ExtProcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < grpc_upstream_count_; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(ConfigOptions config_option = {},
                        const std::vector<std::pair<int, int>>& cluster_endpoints = {{0, 1},
                                                                                     {1, 1}}) {
    int total_cluster_endpoints = 0;
    std::for_each(
        cluster_endpoints.begin(), cluster_endpoints.end(),
        [&total_cluster_endpoints](const auto& item) { total_cluster_endpoints += item.second; });
    ASSERT_EQ(total_cluster_endpoints, grpc_upstream_count_);

    config_helper_.addConfigModifier([this, cluster_endpoints, config_option](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Clusters for ExtProc gRPC servers, starting by copying an existing
      // cluster
      for (const auto& [cluster_id, endpoint_count] : cluster_endpoints) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("ext_proc_server_", cluster_id);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
        ASSERT_EQ(server_cluster->load_assignment().endpoints_size(), 1);
        auto* endpoints = server_cluster->mutable_load_assignment()->mutable_endpoints(0);
        ASSERT_EQ(endpoints->lb_endpoints_size(), 1);
        for (int i = 1; i < endpoint_count; ++i) {
          auto* new_lb_endpoint = endpoints->add_lb_endpoints();
          new_lb_endpoint->MergeFrom(endpoints->lb_endpoints(0));
        }
      }

      const std::string valid_grpc_cluster_name = "ext_proc_server_0";
      if (config_option.valid_grpc_server) {
        // Load configuration of the server from YAML and use a helper to add a grpc_service
        // stanza pointing to the cluster that we just made
        setGrpcService(*proto_config_.mutable_grpc_service(), valid_grpc_cluster_name,
                       grpc_upstreams_[0]->localAddress());
      } else {
        // Set up the gRPC service with wrong cluster name and address.
        setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_wrong_server",
                       std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
      }

      std::string ext_proc_filter_name = "envoy.filters.http.ext_proc";
      if (config_option.downstream_filter) {
        // Construct a configuration proto for our filter and then re-write it
        // to JSON so that we can add it to the overall config
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
            ext_proc_filter;
        ext_proc_filter.set_name(ext_proc_filter_name);
        ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
        config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
      }

      // Add set_metadata filter to inject dynamic metadata used for testing
      if (config_option.add_metadata) {
        envoy::config::listener::v3::Filter set_metadata_filter;
        std::string set_metadata_filter_name = "envoy.filters.http.set_metadata";
        set_metadata_filter.set_name(set_metadata_filter_name);

        envoy::extensions::filters::http::set_metadata::v3::Config set_metadata_config;
        auto* untyped_md = set_metadata_config.add_metadata();
        untyped_md->set_metadata_namespace("forwarding_ns_untyped");
        untyped_md->set_allow_overwrite(true);
        ProtobufWkt::Struct test_md_val;
        (*test_md_val.mutable_fields())["foo"].set_string_value("value from set_metadata");
        (*untyped_md->mutable_value()) = test_md_val;

        auto* typed_md = set_metadata_config.add_metadata();
        typed_md->set_metadata_namespace("forwarding_ns_typed");
        typed_md->set_allow_overwrite(true);
        envoy::extensions::filters::http::set_metadata::v3::Metadata typed_md_to_stuff;
        typed_md_to_stuff.set_metadata_namespace("typed_value from set_metadata");
        typed_md->mutable_typed_value()->PackFrom(typed_md_to_stuff);

        set_metadata_filter.mutable_typed_config()->PackFrom(set_metadata_config);
        config_helper_.prependFilter(
            MessageUtil::getJsonStringFromMessageOrError(set_metadata_filter));

        // Add filter that dumps streamInfo into headers so we can check our receiving
        // namespaces
        config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
        )EOF"));
      }

      // Add logging test filter only in Envoy gRPC mode.
      // gRPC side stream logging is only supported in Envoy gRPC mode at the moment.
      if (clientType() == Grpc::ClientType::EnvoyGrpc && config_option.add_logging_filter &&
          config_option.valid_grpc_server) {
        test::integration::filters::LoggingTestFilterConfig logging_filter_config;
        logging_filter_config.set_logging_id(ext_proc_filter_name);
        logging_filter_config.set_upstream_cluster_name(valid_grpc_cluster_name);
        // No need to check the bytes received for observability mode because it is a
        // "send and go" mode.
        logging_filter_config.set_check_received_bytes(!proto_config_.observability_mode());
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter logging_filter;
        logging_filter.set_name("logging-test-filter");
        logging_filter.mutable_typed_config()->PackFrom(logging_filter_config);

        config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(logging_filter));
      }

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });

    if (config_option.http1_codec) {
      setUpstreamProtocol(Http::CodecType::HTTP1);
      setDownstreamProtocol(Http::CodecType::HTTP1);
    } else {
      setUpstreamProtocol(Http::CodecType::HTTP2);
      setDownstreamProtocol(Http::CodecType::HTTP2);
    }
  }

  bool IsEnvoyGrpc() {
    return std::get<1>(std::get<0>(GetParam())) == Envoy::Grpc::ClientType::EnvoyGrpc;
  }

  void setPerRouteConfig(Route* route, const ExtProcPerRoute& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
  }

  void setPerHostConfig(VirtualHost& vh, const ExtProcPerRoute& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    vh.mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers,
      bool add_content_length = false) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    headers.setMethod("POST");
    if (modify_headers) {
      (*modify_headers)(headers);
    }

    if (add_content_length) {
      headers.setContentLength(body.size());
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBodyAndTrailer(absl::string_view body) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);

    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, body, false);
    Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
    codec_client_->sendTrailers(*request_encoder_, request_trailers);

    return response;
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  void handleUpstreamRequest(bool add_content_length = false) {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    Http::TestResponseHeaderMapImpl response_headers =
        Http::TestResponseHeaderMapImpl{{":status", "200"}};
    uint64_t content_length = 100;
    if (add_content_length) {
      response_headers.setContentLength(content_length);
    }
    upstream_request_->encodeHeaders(response_headers, false);
    upstream_request_->encodeData(content_length, true);
  }

  void verifyChunkedEncoding(const Http::RequestOrResponseHeaderMap& headers) {
    EXPECT_EQ(headers.ContentLength(), nullptr);
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().TransferEncoding,
                                       Http::Headers::get().TransferEncodingValues.Chunked));
  }

  void handleUpstreamRequestWithTrailer() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, false);
    upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
  }

  void handleUpstreamRequestWithResponse(const Buffer::Instance& all_data, uint64_t chunk_size) {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

    // Copy the data so that we don't modify it
    Buffer::OwnedImpl total_response = all_data;
    while (total_response.length() > 0) {
      auto to_move = std::min(total_response.length(), chunk_size);
      Buffer::OwnedImpl chunk;
      chunk.move(total_response, to_move);
      EXPECT_EQ(to_move, chunk.length());
      upstream_request_->encodeData(chunk, false);
    }
    upstream_request_->encodeData(0, true);
  }

  void waitForFirstMessage(FakeUpstream& grpc_upstream, ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void processGenericMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const ProcessingRequest&, ProcessingResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    const bool sendReply = !cb || (*cb)(request, response);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processRequestHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_request_headers();
    const bool sendReply = !cb || (*cb)(request.request_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processRequestTrailersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_trailers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_request_trailers();
    const bool sendReply = !cb || (*cb)(request.request_trailers(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_response_headers();
    const bool sendReply = !cb || (*cb)(request.response_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void
  processRequestBodyMessage(FakeUpstream& grpc_upstream, bool first_message,
                            absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb,
                            bool check_downstream_flow_control = false) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }

    if (check_downstream_flow_control) {
      // Check the flow control counter in downstream, which is triggered on the request
      // path to ext_proc server (i.e., from side stream).
      test_server_->waitForCounterGe(
          "http.config_test.downstream_flow_control_paused_reading_total", 1);
    }

    // Send back the response from ext_proc server.
    ProcessingResponse response;
    auto* body = response.mutable_request_body();
    const bool sendReply = !cb || (*cb)(request.request_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseBodyMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_response_body();
    const bool sendReply = !cb || (*cb)(request.response_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseTrailersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_trailers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_response_trailers();
    const bool sendReply = !cb || (*cb)(request.response_trailers(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processAndRespondImmediately(FakeUpstream& grpc_upstream, bool first_message,
                                    absl::optional<std::function<void(ImmediateResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* immediate = response.mutable_immediate_response();
    if (cb) {
      (*cb)(*immediate);
    }
    processor_stream_->sendGrpcMessage(response);
  }

  // ext_proc server sends back a response to tell Envoy to stop the
  // original timer and start a new timer.
  void serverSendNewTimeout(const uint64_t timeout_ms) {
    ProcessingResponse response;
    if (timeout_ms < 1000) {
      response.mutable_override_message_timeout()->set_nanos(timeout_ms * 1000000);
    } else {
      response.mutable_override_message_timeout()->set_seconds(timeout_ms / 1000);
    }
    processor_stream_->sendGrpcMessage(response);
  }

  // The new timeout message is ignored by Envoy due to different reasons, like
  // new_timeout setting is out-of-range, or max_message_timeout is not configured.
  void newTimeoutWrongConfigTest(const uint64_t timeout_ms) {
    // Set envoy filter timeout to be 200ms.
    proto_config_.mutable_message_timeout()->set_nanos(200000000);
    // Config max_message_timeout proto to enable the new timeout API.
    if (max_message_timeout_ms_) {
      if (max_message_timeout_ms_ < 1000) {
        proto_config_.mutable_max_message_timeout()->set_nanos(max_message_timeout_ms_ * 1000000);
      } else {
        proto_config_.mutable_max_message_timeout()->set_seconds(max_message_timeout_ms_ / 1000);
      }
    }
    initializeConfig();
    HttpIntegrationTest::initialize();
    auto response = sendDownstreamRequest(absl::nullopt);

    processRequestHeadersMessage(*grpc_upstreams_[0], true,
                                 [&](const HttpHeaders&, HeadersResponse&) {
                                   serverSendNewTimeout(timeout_ms);
                                   // ext_proc server stays idle for 300ms before sending back the
                                   // response.
                                   timeSystem().advanceTimeWaitImpl(300ms);
                                   return true;
                                 });
    // Verify the new timer is not started and the original timer timeouts,
    // and downstream receives 504.
    verifyDownstreamResponse(*response, 504);
  }

  void addMutationSetHeaders(const int count,
                             envoy::service::ext_proc::v3::HeaderMutation& mutation) {
    for (int i = 0; i < count; i++) {
      auto* headers = mutation.add_set_headers();
      auto str = absl::StrCat("x-test-header-internal-", std::to_string(i));
      headers->mutable_header()->set_key(str);
      headers->mutable_header()->set_raw_value(str);
    }
  }

  // Verify content-length header set by external processor is removed and chunked encoding is
  // enabled.
  void testWithHeaderMutation(ConfigOptions config_option) {
    initializeConfig(config_option);
    HttpIntegrationTest::initialize();

    auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
    processRequestHeadersMessage(
        *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
          auto* content_length =
              headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
          content_length->mutable_header()->set_key("content-length");
          content_length->mutable_header()->set_raw_value("13");
          return true;
        });

    processRequestBodyMessage(
        *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
          EXPECT_TRUE(body.end_of_stream());
          auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
          body_mut->set_body("Hello, World!");
          return true;
        });
    handleUpstreamRequest();
    // Verify that the content length header is removed and chunked encoding is enabled by http1
    // codec.
    verifyChunkedEncoding(upstream_request_->headers());

    EXPECT_EQ(upstream_request_->body().toString(), "Hello, World!");
    verifyDownstreamResponse(*response, 200);
  }

  // Verify existing content-length header (i.e., no external processor mutation) is removed and
  // chunked encoding is enabled.
  void testWithoutHeaderMutation(ConfigOptions config_option) {
    initializeConfig(config_option);
    HttpIntegrationTest::initialize();

    auto response =
        sendDownstreamRequestWithBody("test!", absl::nullopt, /*add_content_length=*/true);
    processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
    processRequestBodyMessage(
        *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
          EXPECT_TRUE(body.end_of_stream());
          auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
          body_mut->set_body("Hello, World!");
          return true;
        });

    handleUpstreamRequest();
    verifyChunkedEncoding(upstream_request_->headers());

    EXPECT_EQ(upstream_request_->body().toString(), "Hello, World!");
    verifyDownstreamResponse(*response, 200);
  }

  void addMutationRemoveHeaders(const int count,
                                envoy::service::ext_proc::v3::HeaderMutation& mutation) {
    for (int i = 0; i < count; i++) {
      mutation.add_remove_headers(absl::StrCat("x-test-header-internal-", std::to_string(i)));
    }
  }

  void testGetAndFailStream() {
    HttpIntegrationTest::initialize();
    auto response = sendDownstreamRequest(absl::nullopt);

    ProcessingRequest request_headers_msg;
    waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
    // Fail the stream immediately
    processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
    verifyDownstreamResponse(*response, 500);
  }

  void testGetAndCloseStream() {
    HttpIntegrationTest::initialize();
    auto response = sendDownstreamRequest(absl::nullopt);

    ProcessingRequest request_headers_msg;
    waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
    // Just close the stream without doing anything
    processor_stream_->startGrpcStream();
    processor_stream_->finishGrpcStream(Grpc::Status::Ok);

    handleUpstreamRequest();
    verifyDownstreamResponse(*response, 200);
  }

  void testSendDyanmicMetadata() {
    ProtobufWkt::Struct test_md_struct;
    (*test_md_struct.mutable_fields())["foo"].set_string_value("value from ext_proc");

    ProtobufWkt::Value md_val;
    *(md_val.mutable_struct_value()) = test_md_struct;

    processGenericMessage(
        *grpc_upstreams_[0], true,
        [md_val](const ProcessingRequest& req, ProcessingResponse& resp) {
          // Verify the processing request contains the untyped metadata we injected.
          EXPECT_TRUE(req.metadata_context().filter_metadata().contains("forwarding_ns_untyped"));
          const ProtobufWkt::Struct& fwd_metadata =
              req.metadata_context().filter_metadata().at("forwarding_ns_untyped");
          EXPECT_EQ(1, fwd_metadata.fields_size());
          EXPECT_TRUE(fwd_metadata.fields().contains("foo"));
          EXPECT_EQ("value from set_metadata", fwd_metadata.fields().at("foo").string_value());

          // Verify the processing request contains the typed metadata we injected.
          EXPECT_TRUE(
              req.metadata_context().typed_filter_metadata().contains("forwarding_ns_typed"));
          const ProtobufWkt::Any& fwd_typed_metadata =
              req.metadata_context().typed_filter_metadata().at("forwarding_ns_typed");
          EXPECT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Metadata",
                    fwd_typed_metadata.type_url());
          envoy::extensions::filters::http::set_metadata::v3::Metadata typed_md_from_req;
          fwd_typed_metadata.UnpackTo(&typed_md_from_req);
          EXPECT_EQ("typed_value from set_metadata", typed_md_from_req.metadata_namespace());

          // Spoof the response to contain receiving metadata.
          HeadersResponse headers_resp;
          (*resp.mutable_request_headers()) = headers_resp;
          auto mut_md_fields = resp.mutable_dynamic_metadata()->mutable_fields();
          (*mut_md_fields).emplace("receiving_ns_untyped", md_val);

          return true;
        });
  }

  void testSidestreamPushbackDownstream(uint32_t body_size, bool check_downstream_flow_control) {
    config_helper_.setBufferLimits(1024, 1024);

    proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
    proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
    proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
    initializeConfig();
    HttpIntegrationTest::initialize();

    std::string body_str = std::string(body_size, 'a');
    auto response = sendDownstreamRequestWithBody(body_str, absl::nullopt);

    bool end_stream = false;
    int count = 0;
    while (!end_stream) {
      processRequestBodyMessage(
          *grpc_upstreams_[0], count == 0 ? true : false,
          [&end_stream](const HttpBody& body, BodyResponse&) {
            end_stream = body.end_of_stream();
            return true;
          },
          check_downstream_flow_control);
      count++;
    }
    handleUpstreamRequest();

    verifyDownstreamResponse(*response, 200);
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  uint32_t max_message_timeout_ms_{0};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
  TestScopedRuntime scoped_runtime_;
  // Number of grpc upstreams in the test.
  int grpc_upstream_count_ = 2;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, ExtProcIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by immediately closing the stream.
TEST_P(ExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  testGetAndCloseStream();
}

TEST_P(ExtProcIntegrationTest, GetAndCloseStreamWithTracing) {
  // Turn on debug to troubleshoot possible flaky test.
  // TODO(cainelli): Remove this and the debug logs in the tracer test filter after a test failure
  // occurs.
  LogLevelSetter save_levels(spdlog::level::trace);
  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing Initializing config");
  initializeConfig();

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing configuring test tracer");
  config_helper_.addConfigModifier([&](HttpConnectionManager& cm) {
    test::integration::filters::ExpectSpan ext_proc_span;
    ext_proc_span.set_operation_name(
        "async envoy.service.ext_proc.v3.ExternalProcessor.Process egress");
    ext_proc_span.set_context_injected(true);
    ext_proc_span.set_sampled(true);
    ext_proc_span.mutable_tags()->insert({"grpc.status_code", "0"});
    ext_proc_span.mutable_tags()->insert({"upstream_cluster", "ext_proc_server_0"});
    if (IsEnvoyGrpc()) {
      ext_proc_span.mutable_tags()->insert({"upstream_address", "ext_proc_server_0"});
    } else {
      ext_proc_span.mutable_tags()->insert(
          {"upstream_address", grpc_upstreams_[0]->localAddress()->asString()});
    }
    test::integration::filters::TracerTestConfig test_config;
    test_config.mutable_expect_spans()->Add()->CopyFrom(ext_proc_span);

    auto* tracing = cm.mutable_tracing();
    tracing->mutable_provider()->set_name("tracer-test-filter");
    tracing->mutable_provider()->mutable_typed_config()->PackFrom(test_config);
  });

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing initializing http integration test");
  HttpIntegrationTest::initialize();

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing sending downstream request");
  auto response = sendDownstreamRequest(absl::nullopt);

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing waiting for first message");
  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing starting gRPC stream");
  processor_stream_->startGrpcStream();
  EXPECT_FALSE(processor_stream_->headers().get(LowerCaseString("traceparent")).empty())
      << "expected traceparent header";

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing finishing gRPC stream");
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing handling upstream request");
  handleUpstreamRequest();

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing verifying downstream response");
  verifyDownstreamResponse(*response, 200);

  ENVOY_LOG(trace, "GetAndCloseStreamWithTracing done");
}

TEST_P(ExtProcIntegrationTest, GetAndCloseStreamWithLogging) {
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  testGetAndCloseStream();
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by returning a failure before the first stream response can be sent.
TEST_P(ExtProcIntegrationTest, GetAndFailStream) {
  initializeConfig();
  testGetAndFailStream();
}

TEST_P(ExtProcIntegrationTest, GetAndFailStreamWithTracing) {
  initializeConfig();
  config_helper_.addConfigModifier([&](HttpConnectionManager& cm) {
    test::integration::filters::ExpectSpan ext_proc_span;
    ext_proc_span.set_operation_name(
        "async envoy.service.ext_proc.v3.ExternalProcessor.Process egress");
    ext_proc_span.set_context_injected(true);
    ext_proc_span.set_sampled(true);
    ext_proc_span.mutable_tags()->insert({"grpc.status_code", "2"});
    ext_proc_span.mutable_tags()->insert({"error", "true"});
    ext_proc_span.mutable_tags()->insert({"upstream_cluster", "ext_proc_server_0"});
    if (IsEnvoyGrpc()) {
      ext_proc_span.mutable_tags()->insert({"upstream_address", "ext_proc_server_0"});
    } else {
      ext_proc_span.mutable_tags()->insert(
          {"upstream_address", grpc_upstreams_[0]->localAddress()->asString()});
    }

    test::integration::filters::TracerTestConfig test_config;
    test_config.mutable_expect_spans()->Add()->CopyFrom(ext_proc_span);

    auto* tracing = cm.mutable_tracing();
    tracing->mutable_provider()->set_name("tracer-test-filter");
    tracing->mutable_provider()->mutable_typed_config()->PackFrom(test_config);
  });

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  EXPECT_FALSE(processor_stream_->headers().get(LowerCaseString("traceparent")).empty())
      << "expected traceparent header";

  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
  verifyDownstreamResponse(*response, 500);
}

TEST_P(ExtProcIntegrationTest, GetAndFailStreamWithLogging) {
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  testGetAndFailStream();
}

// Test the filter connecting to an invalid ext_proc server that will result in open stream failure.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamWithInvalidServer) {
  ConfigOptions config_option = {};
  config_option.valid_grpc_server = false;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  ProcessingRequest request_headers_msg;
  // Failure is expected when it is connecting to invalid gRPC server. Therefore, default timeout
  // is not used here.
  EXPECT_FALSE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_,
                                                         std::chrono::milliseconds(25000)));
}

TEST_P(ExtProcIntegrationTest, GetAndFailStreamWithInvalidServerOnResponse) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);

  ConfigOptions config_option = {};
  config_option.valid_grpc_server = false;
  config_option.http1_codec = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  handleUpstreamRequest();
  EXPECT_FALSE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_,
                                                         std::chrono::milliseconds(25000)));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLine) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLineLater) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but closes the stream after response_headers.
TEST_P(ExtProcIntegrationTest, GetAndCloseStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but then fails on the response_headers message.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by requesting to modify the request headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeaders) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {"host", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

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

TEST_P(ExtProcIntegrationTest, ResponseFromExtProcServerTooLarge) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    GTEST_SKIP() << "max_receive_message_length is only supported on Envoy gRPC";
  }
  config_helper_.setBufferLimits(1024, 1024);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_grpc_service()
      ->mutable_envoy_grpc()
      ->mutable_max_receive_message_length()
      ->set_value(1024);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = std::string(64 * 1024, 'a');
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], true, [&body_str](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        // Send the over-limit response from ext_proc server.
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body(body_str);
        return true;
      });

  verifyDownstreamResponse(*response, 500);
}

TEST_P(ExtProcIntegrationTest, SetHostHeaderRoutingSucceeded) {
  proto_config_.mutable_mutation_rules()->mutable_allow_all_routing()->set_value(true);
  initializeConfig();
  std::string vhost_domain = "new_host";
  config_helper_.addConfigModifier([&vhost_domain](HttpConnectionManager& cm) {
    // Set up vhost domain.
    auto* vhost = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    vhost->set_name("vhost");
    vhost->clear_domains();
    vhost->add_domains(vhost_domain);
  });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true,
      [&vhost_domain](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                                {":method", "GET"},
                                                                {"host", "host"},
                                                                {":path", "/"},
                                                                {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();

        // Set host header to match the domain of virtual host in routing configuration.
        auto* mut = response_header_mutation->add_set_headers();
        mut->mutable_header()->set_key(":authority");
        mut->mutable_header()->set_raw_value(vhost_domain);

        // Clear the route cache to trigger the route re-pick.
        headers_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Host header is updated when `allow_all_routing` mutation rule is true.
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":authority", "new_host"));

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

TEST_P(ExtProcIntegrationTest, SetHostHeaderRoutingFailed) {
  proto_config_.mutable_mutation_rules()->mutable_allow_all_routing()->set_value(true);
  // Skip the header processing on response path.
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  // Set up the route config.
  std::string vhost_domain = "new_host";
  config_helper_.addConfigModifier([&vhost_domain](HttpConnectionManager& cm) {
    // Set up vhost domain.
    auto* vhost = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    vhost->set_name("vhost");
    vhost->clear_domains();
    vhost->add_domains(vhost_domain);
  });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                                {":method", "GET"},
                                                                {"host", "host"},
                                                                {":path", "/"},
                                                                {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();

        // Set host header to the wrong value that doesn't match the domain of virtual host in route
        // configuration.
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key(":authority");
        mut1->mutable_header()->set_raw_value("wrong_host");

        // Clear the route cache to trigger the route re-pick.
        headers_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });

  // The routing to upstream is expected to fail and 404 is returned to downstream client, since no
  // route is found for mismatched vhost.
  verifyDownstreamResponse(*response, 404);
}

TEST_P(ExtProcIntegrationTest, GetAndSetPathHeader) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                                {":method", "GET"},
                                                                {"host", "host"},
                                                                {":path", "/"},
                                                                {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key(":path");
        mut1->mutable_header()->set_raw_value("/mutated_path/bluh");

        auto* mut2 = response_header_mutation->add_set_headers();
        mut2->mutable_header()->set_key(":scheme");
        mut2->mutable_header()->set_raw_value("https");

        auto* mut3 = response_header_mutation->add_set_headers();
        mut3->mutable_header()->set_key(":authority");
        mut3->mutable_header()->set_raw_value("new_host");

        auto* mut4 = response_header_mutation->add_set_headers();
        mut4->mutable_header()->set_key(":method");
        mut4->mutable_header()->set_raw_value("POST");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Path header is updated.
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":path", "/mutated_path/bluh"));
  // Routing headers are not updated by ext_proc when `allow_all_routing` mutation rule is false
  // (default value).
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":scheme", "http"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":authority", "host"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":method", "GET"));

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

TEST_P(ExtProcIntegrationTest, GetAndSetHeadersWithLogging) {
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

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

TEST_P(ExtProcIntegrationTest, GetAndSetHeadersNonUtf8WithValueInString) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest([](Http::HeaderMap& headers) {
    std::string invalid_unicode("valid_prefix");
    invalid_unicode.append(1, char(0xc3));
    invalid_unicode.append(1, char(0x28));
    invalid_unicode.append("valid_suffix");

    headers.addCopy(LowerCaseString("x-bad-utf8"), invalid_unicode);
  });

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"},
            {":method", "GET"},
            {"host", "host"},
            {":path", "/"},
            {"x-bad-utf8", "valid_prefix\303(valid_suffix"},
            {"x-forwarded-proto", "http"}};
        for (const auto& header : headers.headers().headers()) {
          EXPECT_TRUE(!header.raw_value().empty());
        }
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        response_header_mutation->add_remove_headers("x-bad-utf8");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-bad-utf8"));

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

TEST_P(ExtProcIntegrationTest, GetAndSetHeadersNonUtf8WithValueInBytes) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest([](Http::HeaderMap& headers) {
    std::string invalid_unicode("valid_prefix");
    invalid_unicode.append(1, char(0xc3));
    invalid_unicode.append(1, char(0x28));
    invalid_unicode.append("valid_suffix");

    headers.addCopy(LowerCaseString("x-bad-utf8"), invalid_unicode);
  });

  // Verify the encoded non-utf8 character is received by the server as it is. Then send back a
  // response with non-utf8 character in the header value, and verify it is received by Envoy as it
  // is.
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"},
            {":method", "GET"},
            {"host", "host"},
            {":path", "/"},
            {"x-bad-utf8", "valid_prefix\303(valid_suffix"},
            {"x-forwarded-proto", "http"}};
        for (const auto& header : headers.headers().headers()) {
          EXPECT_TRUE(!header.raw_value().empty());
        }
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        response_header_mutation->add_remove_headers("x-bad-utf8");
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-utf8");
        // Construct a non-utf8 header value and send back to Envoy.
        std::string invalid_unicode("valid_prefix");
        invalid_unicode.append(1, char(0xc3));
        invalid_unicode.append(1, char(0x28));
        invalid_unicode.append("valid_suffix");
        mut1->mutable_header()->set_raw_value(invalid_unicode);
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-bad-utf8"));
  EXPECT_THAT(upstream_request_->headers(),
              SingleHeaderValueIs("x-new-utf8", "valid_prefix\303(valid_suffix"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Test the filter with body buffering turned on, but sending a GET
// and a response that both have no body.
TEST_P(ExtProcIntegrationTest, GetBufferedButNoBodies) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-length", "0"},
      },
      true);

  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_TRUE(headers.end_of_stream());
                                  return true;
                                });

  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, RemoveRequestContentLengthInStreamedMode) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  testWithoutHeaderMutation(config_option);
}

// Test the request content length is removed in BUFFERED BodySendMode + SKIP HeaderSendMode..
TEST_P(ExtProcIntegrationTest, RemoveRequestContentLengthInBufferedMode) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequestWithBody("test!", absl::nullopt, /*add_content_length=*/true);
  processRequestBodyMessage(
      *grpc_upstreams_[0], true, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  handleUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().ContentLength(), nullptr);
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, World!");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, RemoveRequestContentLengthInBufferedPartialMode) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  testWithoutHeaderMutation(config_option);
}

TEST_P(ExtProcIntegrationTest, RemoveRequestContentLengthAfterStreamedProcessing) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  testWithHeaderMutation(config_option);
}

TEST_P(ExtProcIntegrationTest, RemoveRequestContentLengthAfterBufferedPartialProcessing) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  testWithHeaderMutation(config_option);
}

TEST_P(ExtProcIntegrationTest, RemoveResponseContentLength) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);

  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("test!", absl::nullopt);

  handleUpstreamRequest(/*add_content_length=*/true);
  processResponseHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);

  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  verifyChunkedEncoding(response->headers());
  EXPECT_EQ(response->body(), "Hello, World!");
}

TEST_P(ExtProcIntegrationTest, RemoveResponseContentLengthAfterBodyProcessing) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);

  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("test!", absl::nullopt);

  handleUpstreamRequest();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("13");
        return true;
      });

  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  verifyChunkedEncoding(response->headers());
  EXPECT_EQ(response->body(), "Hello, World!");
}

TEST_P(ExtProcIntegrationTest, MismatchedContentLengthAndBodyLength) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  ConfigOptions config_option = {};
  config_option.http1_codec = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  std::string modified_body = "Hello, World!";
  // The content_length set by ext_proc server doesn't match the length of mutated body.
  int set_content_length = modified_body.size() - 2;
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [&](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value(absl::StrCat(set_content_length));
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [&](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body(modified_body);
        return true;
      });
  EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                         std::chrono::milliseconds(25000)));
  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by requesting to modify the response headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_raw_value("201");
        return true;
      });

  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// but tries to set the status code to an invalid value
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponseBadStatus) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_raw_value("100");
        return true;
      });

  // Invalid status code should be ignored, but the other header mutation
  // should still have been processed.
  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// but tries to set the status code to two values. The second
// attempt should be ignored.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponseTwoStatuses) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_raw_value("201");
        auto* add3 = response_mutation->add_set_headers();
        add3->mutable_header()->set_key(":status");
        add3->mutable_header()->set_raw_value("202");
        add3->mutable_append()->set_value(true);
        return true;
      });

  // Invalid status code should be ignored, but the other header mutation
  // should still have been processed.
  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by checking the headers and modifying the trailers
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersAndTrailersOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that tries to modify the trailers incorrectly
// according to the header mutation rules.
// TODO(tyxia): re-enable this test (see https://github.com/envoyproxy/envoy/issues/35281)
TEST_P(ExtProcIntegrationTest, DISABLED_GetAndSetTrailersIncorrectlyOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_mutation_rules()->mutable_disallow_all()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers&, TrailersResponse& resp) {
        auto* trailer_add = resp.mutable_header_mutation()->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // We get a reset since we've received some of the response already.
    ASSERT_TRUE(response->waitForReset());
  } else {
    verifyDownstreamResponse(*response, 500);
  }
}

// Test the filter configured to only send the response trailers message
TEST_P(ExtProcIntegrationTest, GetAndSetOnlyTrailersOnResponse) {
  auto* mode = proto_config_.mutable_processing_mode();
  mode->set_request_header_mode(ProcessingMode::SKIP);
  mode->set_response_header_mode(ProcessingMode::SKIP);
  mode->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequestWithTrailer();
  processResponseTrailersMessage(
      *grpc_upstreams_[0], true, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("13");
        return true;
      });

  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_raw_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
  // Verify that the content length header in the response is set by external processor,
  EXPECT_EQ(response->headers().getContentLengthValue(), "13");
  EXPECT_EQ("Hello, World!", response->body());
}

TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("Hello, World!", response->body());
}

// Test the filter with a response body callback enabled that uses
// partial buffering. We should still be able to change headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnResponsePartialBuffered) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("100");
        return true;
      });
  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_raw_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  // Verify that the content length header is removed in BUFFERED_PARTIAL BodySendMode.
  EXPECT_EQ(response->headers().ContentLength(), nullptr);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersAndTrailersOnResponse) {
  auto* mode = proto_config_.mutable_processing_mode();
  mode->set_response_body_mode(ProcessingMode::BUFFERED);
  mode->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  // Should get just one message with the body
  processResponseBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_FALSE(body.end_of_stream());
    return true;
  });

  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse& resp) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        auto* trailer_mut = resp.mutable_header_mutation();
        auto* trailer_add = trailer_mut->add_set_headers();
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-test-trailers", "Yes"));
  EXPECT_THAT(*(response->trailers()), SingleHeaderValueIs("x-modified-trailers", "xxx"));
}

// Test the filter using a configuration that sends response headers and trailers,
// and process an upstream response that has no trailers.
TEST_P(ExtProcIntegrationTest, NoTrailersOnResponseWithModeSendHeaderTrailer) {
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using a configuration that sends response body and trailers, and process
// an upstream response that has no trailers.
TEST_P(ExtProcIntegrationTest, NoTrailersOnResponseWithModeSendBodyTrailer) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();
  processResponseBodyMessage(*grpc_upstreams_[0], true, absl::nullopt);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body and headers, using a response
// big enough to require multiple chunks.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyAndHeadersOnBigResponse) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);

  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 4000);
  handleUpstreamRequestWithResponse(full_response, 1000);

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Should get just one message with the body
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_raw_value("Yes");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-testing-response-header", "Yes"));
}

// Test the filter with both body callbacks enabled and have the
// ext_proc server change both of them.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnBoth) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("13");
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_header_mutation()->add_remove_headers(
            "content-length");
        return true;
      });

  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        body_resp.mutable_response()->mutable_body_mutation()->set_body("123");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("123", response->body());
}

// Test the filter using a configuration that uses the processing mode to
// only send the response_headers message.
TEST_P(ExtProcIntegrationTest, ProcessingModeResponseOnly) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message, which should be
// returned directly to the downstream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediately) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
    auto* hdr1 = immediate.mutable_headers()->add_set_headers();
    hdr1->mutable_header()->set_key("x-failure-reason");
    hdr1->mutable_header()->set_raw_value("testing");
    auto* hdr2 = immediate.mutable_headers()->add_set_headers();
    hdr2->mutable_header()->set_key("content-type");
    hdr2->mutable_header()->set_raw_value("application/json");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-failure-reason", "testing"));
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("content-type", "application/json"));
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithLogging) {
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
    auto* hdr1 = immediate.mutable_headers()->add_set_headers();
    hdr1->mutable_header()->set_key("x-failure-reason");
    hdr1->mutable_header()->set_raw_value("testing");
    auto* hdr2 = immediate.mutable_headers()->add_set_headers();
    hdr2->mutable_header()->set_key("content-type");
    hdr2->mutable_header()->set_raw_value("application/json");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-failure-reason", "testing"));
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("content-type", "application/json"));
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithInvalidCharacter) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    auto* hdr = immediate.mutable_headers()->add_set_headers();
    hdr->mutable_header()->set_key("x-failure-reason\n");
    hdr->mutable_header()->set_raw_value("testing");
  });

  verifyDownstreamResponse(*response, 401);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message after the
// request_headers message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by sending back an immediate_response message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnRequestBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body buffering enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since we
// are in buffered mode, we should get the correct response code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // Since we are stopping iteration on headers, and since the response is short,
  // we actually get an error message here.
  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body buffering enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since we
// are in buffered partial mode, we should get the correct response code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponseBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // Since we are stopping iteration on headers, and since the response is short,
  // we actually get an error message here.
  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body buffering enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since we
// are in buffered mode, we should get the correct response code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnChunkedResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 400);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // Since we are stopping iteration on headers, and since the response is short,
  // we actually get an error message here.
  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body buffering enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since we
// are in buffered partial mode, we should get the correct response code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnChunkedResponseBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 400);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  processAndRespondImmediately(*grpc_upstreams_[0], false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // Since we are stopping iteration on headers, and since the response is short,
  // we actually get an error message here.
  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter using an ext_proc server that responds to the request_body message
// by sending back an immediate_response message with an invalid status code.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithBadStatus) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Continue);
    immediate.set_body("{\"reason\": \"Because\"}");
    immediate.set_details("Failed because we said so");
  });

  // The attempt to set the status code to 100 should have been ignored.
  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("{\"reason\": \"Because\"}", response->body());
}

// Test the filter using an ext_proc server that responds to the request_header message
// by sending back an immediate_response message with system header mutation.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithSystemHeaderMutation) {
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  // Disallow system header in the mutation rule config.
  proto_config_.mutable_mutation_rules()->mutable_disallow_system()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    auto* hdr = immediate.mutable_headers()->add_set_headers();
    // Adding system header in the ext_proc response.
    hdr->mutable_header()->set_key(":foo");
    hdr->mutable_header()->set_raw_value("bar");
  });
  verifyDownstreamResponse(*response, 401);
  // The added system header is not sent to the client.
  EXPECT_THAT(response->headers(), HasNoHeader(":foo"));
}

// Test the filter using an ext_proc server that responds to the request_header message
// by sending back an immediate_response message with x-envoy header mutation.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyWithEnvoyHeaderMutation) {
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_allow_envoy()->set_value(false);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    auto* hdr = immediate.mutable_headers()->add_set_headers();
    // Adding x-envoy header is not allowed.
    hdr->mutable_header()->set_key("x-envoy-foo");
    hdr->mutable_header()->set_raw_value("bar");
  });
  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), HasNoHeader("x-envoy-foo"));
}

TEST_P(ExtProcIntegrationTest, GetAndImmediateRespondMutationAllowEnvoy) {
  proto_config_.mutable_mutation_rules()->mutable_allow_envoy()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_allow_all_routing()->set_value(true);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processAndRespondImmediately(*grpc_upstreams_[0], true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    auto* hdr = immediate.mutable_headers()->add_set_headers();
    hdr->mutable_header()->set_key("x-envoy-foo");
    hdr->mutable_header()->set_raw_value("bar");
    auto* hdr1 = immediate.mutable_headers()->add_set_headers();
    hdr1->mutable_header()->set_key("host");
    hdr1->mutable_header()->set_raw_value("test");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("host", "test"));
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-envoy-foo", "bar"));
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by modifying a header that should cause an error.
TEST_P(ExtProcIntegrationTest, GetAndIncorrectlyModifyHeaderOnBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Original body", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& response) {
        EXPECT_TRUE(body.end_of_stream());
        auto* mut = response.mutable_response()->mutable_header_mutation()->add_set_headers();
        mut->mutable_header()->set_key(":scheme");
        mut->mutable_header()->set_raw_value("tcp");
        return true;
      });

  verifyDownstreamResponse(*response, 500);
}

// Test the filter with request body buffering enabled using
// an ext_proc server that responds to the request_body message
// by modifying a header that should cause an error.
TEST_P(ExtProcIntegrationTest, GetAndIncorrectlyModifyHeaderOnBodyPartialBuffer) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Original body", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& response) {
        EXPECT_TRUE(body.end_of_stream());
        auto* mut = response.mutable_response()->mutable_header_mutation()->add_set_headers();
        mut->mutable_header()->set_key(":scheme");
        mut->mutable_header()->set_raw_value("tcp");
        return true;
      });

  verifyDownstreamResponse(*response, 500);
}

// Test the ability of the filter to turn a GET into a POST by adding a body
// and changing the method.
TEST_P(ExtProcIntegrationTest, ConvertGetToPost) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* header_mut = headers_resp.mutable_response()->mutable_header_mutation();
        auto* method = header_mut->add_set_headers();
        method->mutable_header()->set_key(":method");
        method->mutable_header()->set_raw_value("POST");
        auto* content_type = header_mut->add_set_headers();
        content_type->mutable_header()->set_key("content-type");
        content_type->mutable_header()->set_raw_value("text/plain");
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  handleUpstreamRequest();

  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs(":method", "POST"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("content-type", "text/plain"));
  EXPECT_EQ(upstream_request_->bodyLength(), 14);
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test the ability of the filter to completely replace a request message with a new
// request message.
TEST_P(ExtProcIntegrationTest, ReplaceCompleteRequest) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  handleUpstreamRequest();

  // Ensure that we replaced and did not append to the request.
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test the ability of the filter to completely replace a request message with a new
// request message.
TEST_P(ExtProcIntegrationTest, ReplaceCompleteRequestBuffered) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, Server!");
        // This special status tells us to replace the whole request
        headers_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        return true;
      });

  // Even though we set the body mode to BUFFERED, we should receive no callback because
  // we returned CONTINUE_AND_REPLACE.

  handleUpstreamRequest();

  // Ensure that we replaced and did not append to the request.
  EXPECT_EQ(upstream_request_->body().toString(), "Hello, Server!");

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Send a request, but wait longer than the "message timeout" before sending a response
// from the external processor. This should trigger the timeout and result
// in a 500 error.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutOldErrorCode) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.ext_proc_timeout_error", "false"}});
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 500);
}

TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutWithTracing) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();

  config_helper_.addConfigModifier([&](HttpConnectionManager& cm) {
    test::integration::filters::ExpectSpan ext_proc_span;
    ext_proc_span.set_operation_name(
        "async envoy.service.ext_proc.v3.ExternalProcessor.Process egress");
    ext_proc_span.set_context_injected(true);
    ext_proc_span.set_sampled(true);
    ext_proc_span.mutable_tags()->insert({"status", "canceled"});
    ext_proc_span.mutable_tags()->insert({"error", ""}); // not an error
    ext_proc_span.mutable_tags()->insert({"upstream_cluster", "ext_proc_server_0"});
    if (IsEnvoyGrpc()) {
      ext_proc_span.mutable_tags()->insert({"upstream_address", "ext_proc_server_0"});
    } else {
      ext_proc_span.mutable_tags()->insert(
          {"upstream_address", grpc_upstreams_[0]->localAddress()->asString()});
    }
    test::integration::filters::TracerTestConfig test_config;
    test_config.mutable_expect_spans()->Add()->CopyFrom(ext_proc_span);

    auto* tracing = cm.mutable_tracing();
    tracing->mutable_provider()->set_name("tracer-test-filter");
    tracing->mutable_provider()->mutable_typed_config()->PackFrom(test_config);
  });

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  EXPECT_FALSE(processor_stream_->headers().get(LowerCaseString("traceparent")).empty())
      << "expected traceparent header";

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutWithLogging) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutWithLogging) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  ConfigOptions config_option = {};
  config_option.add_logging_filter = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

// Send a request,  wait longer than the "message timeout" before sending a response
// from the external processor, but nothing should happen because we are ignoring
// the timeout.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms
                                 timeSystem().advanceTimeWaitImpl(400ms);
                                 return false;
                               });

  // We should be able to continue from here since the error was ignored
  handleUpstreamRequest();
  // Since we are ignoring errors, the late response to the request headers
  // message meant that subsequent messages are spurious and the response
  // headers message was never sent.
  // Despite the timeout the request should succeed.
  verifyDownstreamResponse(*response, 200);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  // We should still succeed despite the timeout
  verifyDownstreamResponse(*response, 200);
}

// While waiting for a response from the external processor, trigger a
// downstream disconnect followed by a response message timeout.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutDownstreamDisconnect) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [this](const HttpHeaders&, HeadersResponse&) {
                                  // Downstream disconnect
                                  codec_client_->close();
                                  // Travel forward 400 ms
                                  timeSystem().advanceTimeWaitImpl(400ms);
                                  return false;
                                });

  ASSERT_TRUE(response->waitForReset());
}

// Test how the filter responds when asked to buffer a request body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, BufferEmptyBodyNotSendingHeader) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], true, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a response body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetWithEmptyResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(0, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_FALSE(headers.end_of_stream());
                                  return true;
                                });
  // We should get an empty body message this time
  processResponseBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a response body for a POST
// request with no body. We should not get an empty body message because
// the Envoy filter stream received headers with no body.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetWithNoResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders& headers, HeadersResponse&) {
                                  EXPECT_TRUE(headers.end_of_stream());
                                  return true;
                                });
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a POST
// request with an empty body. We should get an empty body message because
// the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBodyStreamed) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a POST
// request with an empty body in "buffered partial" mode. We should get an empty body message
// because the Envoy filter stream received the body after all the headers.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithEmptyBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  // We should get an empty body message this time
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body().size(), 0);
    return true;
  });

  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a request body for a GET
// request with no body. We should receive no body message because the Envoy
// filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a GET
// request with no body. We should receive no body message because the Envoy
// filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBodyStreaming) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to stream a request body for a GET
// request with no body in "buffered partial" mode. We should receive no body message because the
// Envoy filter stream received the headers and end simultaneously.
TEST_P(ExtProcIntegrationTest, BufferBodyOverrideGetRequestNoBodyBufferedPartial) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_TRUE(headers.end_of_stream());
                                 return true;
                               });
  // We should not see a request body message here
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Test how the filter responds when asked to buffer a request body for a POST
// request with a body.
TEST_P(ExtProcIntegrationTest, BufferBodyOverridePostWithRequestBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Testing", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    EXPECT_EQ(body.body(), "Testing");
    return true;
  });
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that sets a custom processing mode on the
// route, and test that the processing mode takes effect.
TEST_P(ExtProcIntegrationTest, PerRouteProcessingMode) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that it will send a buffered body
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
        ProcessingMode::BUFFERED);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Because of the per-route config we should get a buffered response
  processResponseBodyMessage(*grpc_upstreams_[0], false,
                             [&full_response](const HttpBody& body, BodyResponse&) {
                               EXPECT_TRUE(body.end_of_stream());
                               EXPECT_EQ(body.body(), full_response.toString());
                               return true;
                             });
  verifyDownstreamResponse(*response, 200);
}

// Set up configuration on the virtual host and on the route and see that
// the two are merged.
TEST_P(ExtProcIntegrationTest, PerRouteAndHostProcessingMode) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    // Set up a processing mode for the host that should not be honored
    ExtProcPerRoute per_host;
    per_host.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
        ProcessingMode::SKIP);
    per_host.mutable_overrides()->mutable_processing_mode()->set_response_header_mode(
        ProcessingMode::SKIP);
    setPerHostConfig(*vh, per_host);

    // Set up "/foo" so that it will send a buffered body
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
        ProcessingMode::BUFFERED);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  // Because of the per-route config we should get a buffered response.
  // If the config from the host is applied then this won't work.
  processResponseBodyMessage(*grpc_upstreams_[0], false,
                             [&full_response](const HttpBody& body, BodyResponse&) {
                               EXPECT_TRUE(body.end_of_stream());
                               EXPECT_EQ(body.body(), full_response.toString());
                               return true;
                             });
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that disables ext_proc for a route and ensure
// that it is not called.
TEST_P(ExtProcIntegrationTest, PerRouteDisable) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that ext_proc is disabled
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    per_route.set_disabled(true);
    setPerRouteConfig(route, per_route);
  });
  HttpIntegrationTest::initialize();

  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  // There should be no ext_proc processing here
  Buffer::OwnedImpl full_response;
  TestUtility::feedBufferWithRandomCharacters(full_response, 100);
  handleUpstreamRequestWithResponse(full_response, 100);
  verifyDownstreamResponse(*response, 200);
}

// Set up per-route configuration that sets a different GrpcService on the
// route, and verify that it is used instead of the primary GrpcService.
TEST_P(ExtProcIntegrationTest, PerRouteGrpcService) {
  initializeConfig();
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that it will use a different GrpcService
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    setGrpcService(*per_route.mutable_overrides()->mutable_grpc_service(), "ext_proc_server_1",
                   grpc_upstreams_[1]->localAddress());
    setPerRouteConfig(route, per_route);

    // Add logging test filter here in place since it has a different GrpcService from route.
    if (clientType() == Grpc::ClientType::EnvoyGrpc) {
      test::integration::filters::LoggingTestFilterConfig logging_filter_config;
      logging_filter_config.set_logging_id("envoy.filters.http.ext_proc");
      logging_filter_config.set_upstream_cluster_name("ext_proc_server_1");
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter logging_filter;
      logging_filter.set_name("logging-test-filter");
      logging_filter.mutable_typed_config()->PackFrom(logging_filter_config);

      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(logging_filter));
    }
  });
  HttpIntegrationTest::initialize();

  // Request that matches route directed to ext_proc_server_1
  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });
  processRequestHeadersMessage(*grpc_upstreams_[1], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(
      *grpc_upstreams_[1], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* add1 = response_mutation->add_set_headers();
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        auto* add2 = response_mutation->add_set_headers();
        add2->mutable_header()->set_key(":status");
        add2->mutable_header()->set_raw_value("201");
        return true;
      });
  verifyDownstreamResponse(*response, 201);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Set up per-route configuration that extends original metadata.
TEST_P(ExtProcIntegrationTest, PerRouteGrpcMetadata) {
  initializeConfig();

  // Override metadata from route config.
  config_helper_.addConfigModifier([this](HttpConnectionManager& cm) {
    // Set up "/foo" so that it will use a different GrpcService
    auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
    auto* route = vh->mutable_routes()->Mutable(0);
    route->mutable_match()->set_path("/foo");
    ExtProcPerRoute per_route;
    *per_route.mutable_overrides()->mutable_grpc_initial_metadata()->Add() =
        makeHeaderValue("b", "c");
    *per_route.mutable_overrides()->mutable_grpc_initial_metadata()->Add() =
        makeHeaderValue("c", "c");
    setPerRouteConfig(route, per_route);
  });

  HttpIntegrationTest::initialize();

  // Request that matches route directed to ext_proc_server_0
  auto response =
      sendDownstreamRequest([](Http::RequestHeaderMap& headers) { headers.setPath("/foo"); });

  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  EXPECT_EQ(
      "c",
      processor_stream_->headers().get(Http::LowerCaseString("b"))[0]->value().getStringView());
  EXPECT_EQ(
      "c",
      processor_stream_->headers().get(Http::LowerCaseString("c"))[0]->value().getStringView());
  handleUpstreamRequest();

  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Sending new timeout API in both downstream request and upstream response
// handling path with header mutation.
TEST_P(ExtProcIntegrationTest, RequestAndResponseMessageNewTimeoutWithHeaderMutation) {
  // Set envoy filter timeout to be 200ms.
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  // Config max_message_timeout proto to enable the new timeout API.
  proto_config_.mutable_max_message_timeout()->set_seconds(TestUtility::DefaultTimeout.count() /
                                                           1000);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  // ext_proc server request processing.
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [this](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {"host", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        // Sending the new timeout API to extend the timeout.
        serverSendNewTimeout(TestUtility::DefaultTimeout.count());
        // ext_proc server stays idle for 300ms.
        timeSystem().advanceTimeWaitImpl(300ms);
        // Server sends back response with the header mutation instructions.
        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // ext_proc server response processing.
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [this](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        // Sending the new timeout API to extend the timeout.
        serverSendNewTimeout(TestUtility::DefaultTimeout.count());
        // ext_proc server stays idle for 300ms.
        timeSystem().advanceTimeWaitImpl(300ms);
        return true;
      });
  // Verify downstream client receives 200 okay. i.e, no timeout happened.
  verifyDownstreamResponse(*response, 200);
}

// Extending timeout in downstream request handling also no mutation.
TEST_P(ExtProcIntegrationTest, RequestMessageNewTimeoutNoMutation) {
  // Set envoy filter timeout to be 200ms.
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  // Config max_message_timeout proto to enable the new timeout API.
  proto_config_.mutable_max_message_timeout()->set_seconds(TestUtility::DefaultTimeout.count() /
                                                           1000);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Sending the new timeout API to extend the timeout.
                                 serverSendNewTimeout(TestUtility::DefaultTimeout.count());
                                 // ext_proc server stays idle for 300ms before sending back the
                                 // response.
                                 timeSystem().advanceTimeWaitImpl(300ms);
                                 return true;
                               });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders&, HeadersResponse&) { return true; });
  // Verify downstream client receives 200 okay. i.e, no timeout happened.
  verifyDownstreamResponse(*response, 200);
}

// Test only the first new timeout message in one state is accepted.
TEST_P(ExtProcIntegrationTest, RequestMessageNoMutationMultipleNewTimeout) {
  // Set envoy filter timeout to be 200ms.
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  // Config max_message_timeout proto to enable the new timeout API.
  proto_config_.mutable_max_message_timeout()->set_seconds(TestUtility::DefaultTimeout.count() /
                                                           1000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Sending the new big timeout API to extend the timeout.
                                 serverSendNewTimeout(TestUtility::DefaultTimeout.count());
                                 // Server wait for 100ms.
                                 timeSystem().advanceTimeWaitImpl(100ms);
                                 // Send the 2nd 10ms new timeout update.
                                 // The update is ignored by Envoy. No timeout event
                                 // happened and the traffic went through fine.
                                 serverSendNewTimeout(10);
                                 // Server wait for 300ms
                                 timeSystem().advanceTimeWaitImpl(300ms);
                                 return true;
                               });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);
  processResponseHeadersMessage(*grpc_upstreams_[0], false,
                                [](const HttpHeaders&, HeadersResponse&) { return true; });
  verifyDownstreamResponse(*response, 200);
}

// Setting new timeout 0ms which is < 1ms. The message is ignored.
TEST_P(ExtProcIntegrationTest, RequestMessageNewTimeoutNegativeTestTimeoutTooSmall) {
  // Config max_message_timeout proto to 10s to enable the new timeout API.
  max_message_timeout_ms_ = 10000;
  newTimeoutWrongConfigTest(0);
}

// Setting max_message_timeout proto to be 100ms. And send the new timeout 500ms
// which is > max_message_timeout(100ms). The message is ignored.
TEST_P(ExtProcIntegrationTest, RequestMessageNewTimeoutNegativeTestTimeoutTooBigWithSmallMax) {
  // Config max_message_timeout proto to 100ms to enable the new timeout API.
  max_message_timeout_ms_ = 100;
  newTimeoutWrongConfigTest(500);
}

// Not setting the max_message_timeout effectively disabled the new timeout API.
TEST_P(ExtProcIntegrationTest, RequestMessageNewTimeoutNegativeTestTimeoutNotAcceptedDefaultMax) {
  newTimeoutWrongConfigTest(500);
}

// Send the new timeout to be an extremely large number, which is out-of-range of duration.
// Verify the code appropriately handled it.
TEST_P(ExtProcIntegrationTest, RequestMessageNewTimeoutOutOfBounds) {
  // Config max_message_timeout proto to 100ms to enable the new timeout API.
  max_message_timeout_ms_ = 100;
  const uint64_t override_message_timeout = 1000000000000000;
  newTimeoutWrongConfigTest(override_message_timeout);
}

// Set the ext_proc filter in SKIP header, SEND trailer, and BUFFERED body mode.
// Send a request with headers and trailers. No body is sent to the ext_proc server.
TEST_P(ExtProcIntegrationTest, SkipHeaderSendTrailerInBufferedMode) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  processRequestTrailersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Set the ext_proc filter processing mode to send request header, body and trailer.
// Then have the client send header and trailer.
TEST_P(ExtProcIntegrationTest, ClientSendHeaderTrailerFilterConfigedSendAll) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test the filter with the header allow list set and disallow list empty and
// verify only the allowed headers are sent to the ext_proc server.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersAndTrailersWithAllowedHeader) {
  auto* forward_rules = proto_config_.mutable_forward_rules();
  auto* list = forward_rules->mutable_allowed_headers();
  list->add_patterns()->set_exact(":method");
  list->add_patterns()->set_exact(":authority");
  list->add_patterns()->set_exact(":status");
  list->add_patterns()->set_exact("x-test-trailers");
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":method", "GET"},
                                                                {":authority", "host"}};
        // Verify only allowed request headers is received by ext_proc server.
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        return true;
      });
  processRequestTrailersMessage(*grpc_upstreams_[0], false,
                                [](const HttpTrailers& trailers, TrailersResponse&) {
                                  // The request trailer is not in the allow list.
                                  EXPECT_EQ(trailers.trailers().headers_size(), 0);
                                  return true;
                                });
  // Send back response with :status 200 and trailer header: x-test-trailers.
  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse&) {
        // The response trailer is in the allow list.
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-test-trailers", "Yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        return true;
      });
  verifyDownstreamResponse(*response, 200);
}

// Test the filter with header disallow list set and allow list empty and verify
// the disallowed headers are not sent to the ext_proc server.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersAndTrailersWithDisallowedHeader) {
  auto* forward_rules = proto_config_.mutable_forward_rules();
  auto* list = forward_rules->mutable_disallowed_headers();
  list->add_patterns()->set_exact(":method");
  list->add_patterns()->set_exact(":authority");
  list->add_patterns()->set_exact("x-forwarded-proto");
  list->add_patterns()->set_exact("foo");
  list->add_patterns()->set_exact("x-test-trailers");
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                                {":path", "/"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        return true;
      });
  processRequestTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse&) {
        // The request trailer is not in the disallow list. Forwarded.
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-trailer-foo", "yes"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        return true;
      });
  // Send back response with :status 200 and trailer header: x-test-trailers.
  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  processResponseTrailersMessage(*grpc_upstreams_[0], false,
                                 [](const HttpTrailers& trailers, TrailersResponse&) {
                                   // The response trailer is in the disallow list.
                                   EXPECT_EQ(trailers.trailers().headers_size(), 0);
                                   return true;
                                 });
  verifyDownstreamResponse(*response, 200);
}

// Test the filter with both header allow and disallow list set and verify
// the headers in the allow list but not in the disallow list are sent.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersAndTrailersWithBothAllowedAndDisallowedHeader) {
  auto* forward_rules = proto_config_.mutable_forward_rules();
  auto* allow_list = forward_rules->mutable_allowed_headers();
  allow_list->add_patterns()->set_exact(":method");
  allow_list->add_patterns()->set_exact(":authority");
  allow_list->add_patterns()->set_exact(":status");
  allow_list->add_patterns()->set_exact("x-test-trailers");
  allow_list->add_patterns()->set_exact("x-trailer-foo");

  auto* disallow_list = forward_rules->mutable_disallowed_headers();
  disallow_list->add_patterns()->set_exact(":method");
  disallow_list->add_patterns()->set_exact("x-forwarded-proto");
  disallow_list->add_patterns()->set_exact("foo");
  disallow_list->add_patterns()->set_exact("x-test-trailers");

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{
      {"x-test-trailers", "yes"}, {"x-trailer-foo", "no"}, {"x-trailer-bar", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":authority", "host"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));
        return true;
      });
  processRequestTrailersMessage(
      *grpc_upstreams_[0], false, [](const HttpTrailers& trailers, TrailersResponse&) {
        Http::TestResponseTrailerMapImpl expected_trailers{{"x-trailer-foo", "no"}};
        EXPECT_THAT(trailers.trailers(), HeaderProtosEqual(expected_trailers));
        return true;
      });
  // Send back response with :status 200 and trailer header: x-test-trailers.
  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  processResponseTrailersMessage(*grpc_upstreams_[0], false,
                                 [](const HttpTrailers& trailers, TrailersResponse&) {
                                   // The response trailer is in the disallow list.
                                   EXPECT_EQ(trailers.trailers().headers_size(), 0);
                                   return true;
                                 });
  verifyDownstreamResponse(*response, 200);
}

// Test clear route cache in both upstream and downstream header and body processing.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnBothWithClearRouteCache) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("13");
        headers_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        body_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        headers_resp.mutable_response()->mutable_header_mutation()->add_remove_headers(
            "content-length");
        headers_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });
  upstream_request_->encodeData(100, true);
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* header_mut = body_resp.mutable_response()->mutable_header_mutation();
        auto* header_add = header_mut->add_set_headers();
        header_add->mutable_header()->set_key("x-testing-response-header");
        header_add->mutable_header()->set_raw_value("Yes");
        body_resp.mutable_response()->set_clear_route_cache(true);
        return true;
      });
  verifyDownstreamResponse(*response, 200);
}

// Applying header mutations for request|response headers|body|trailers. This test verifies
// the HCM limits are plumbed into the HeaderMap correctly in different scenarios.
TEST_P(ExtProcIntegrationTest, HeaderMutationCheckPassWithHcmSizeConfig) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);

  initializeConfig();

  // Change the HCM max header config to limit the request header size.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(30);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(50);
      });

  // Change the cluster max header config to limit the response header size.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(50);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [this](const HttpHeaders&, HeadersResponse& header_resp) {
        addMutationSetHeaders(20, *header_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [this](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_FALSE(body.end_of_stream());
        addMutationSetHeaders(20, *body_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  processRequestTrailersMessage(
      *grpc_upstreams_[0], false, [this](const HttpTrailers&, TrailersResponse& trailer_resp) {
        addMutationSetHeaders(20, *trailer_resp.mutable_header_mutation());
        return true;
      });

  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [this](const HttpHeaders&, HeadersResponse& header_resp) {
        addMutationSetHeaders(20, *header_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [this](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_FALSE(body.end_of_stream());
        addMutationSetHeaders(20, *body_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [this](const HttpTrailers&, TrailersResponse& trailer_resp) {
        addMutationSetHeaders(20, *trailer_resp.mutable_header_mutation());
        return true;
      });
  verifyDownstreamResponse(*response, 200);
}

// ext_proc server set_header count exceeds the HCM limit when responding to the request header.
TEST_P(ExtProcIntegrationTest, SetHeaderMutationFailWithRequestHeader) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(30);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(50);
      });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [this](const HttpHeaders&, HeadersResponse& headers_resp) {
        // The set header count 60 > HCM limit 50.
        addMutationSetHeaders(60, *headers_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  verifyDownstreamResponse(*response, 500);
}

// ext_proc server remove_header count exceeds the HCM limit when responding to the response header.
TEST_P(ExtProcIntegrationTest, RemoveHeaderMutationFailWithResponseHeader) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(50);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(
      *grpc_upstreams_[0], true, [this](const HttpHeaders&, HeadersResponse& headers_resp) {
        addMutationRemoveHeaders(60, *headers_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  verifyDownstreamResponse(*response, 500);
}

// ext_proc server set_header count exceeds the HCM limit when responding to the request body.
TEST_P(ExtProcIntegrationTest, SetHeaderMutationFailWithRequestBody) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(30);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(50);
      });
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Original body", absl::nullopt);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [this](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        addMutationSetHeaders(60, *body_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  verifyDownstreamResponse(*response, 500);
}

// ext_proc server remove_header count exceeds the HCM limit when responding to the response body.
TEST_P(ExtProcIntegrationTest, RemoveHeaderMutationFailWithResponseBody) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(50);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processResponseBodyMessage(
      *grpc_upstreams_[0], false, [this](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        addMutationRemoveHeaders(60, *body_resp.mutable_response()->mutable_header_mutation());
        return true;
      });
  verifyDownstreamResponse(*response, 500);
}

// ext_proc server header mutation ends up exceeding the HCM header size limit
// when responding to the request trailer.
TEST_P(ExtProcIntegrationTest, HeaderMutationResultSizeFailWithRequestTrailer) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(1);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(50);
      });
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  // Sending a large trailer to help the header mutation eventually exceed the size limit.
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", std::string(950, 'a')}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  processRequestTrailersMessage(*grpc_upstreams_[0], true,
                                [this](const HttpTrailers&, TrailersResponse& trailer_resp) {
                                  // The set header counter 5 is smaller than HCM header count
                                  // limit 50, add size is smaller than the size limit 1kb. It
                                  // passed the mutation check, but failed the end result size
                                  // check.
                                  addMutationSetHeaders(5, *trailer_resp.mutable_header_mutation());
                                  return true;
                                });
  verifyDownstreamResponse(*response, 500);
}

// ext_proc server header mutation ends up exceeding the HCM header count limit
// when responding to the response trailer.
TEST_P(ExtProcIntegrationTest, HeaderMutationResultSizeFailWithResponseTrailer) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(50);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, false);

  Http::TestResponseTrailerMapImpl response_trailers{{"x-trailer-foo-1", "foo-1"},
                                                     {"x-trailer-foo-2", "foo-2"},
                                                     {"x-trailer-foo-3", "foo-3"},
                                                     {"x-trailer-foo-4", "foo-4"},
                                                     {"x-trailer-foo-5", "foo-5"}};
  upstream_request_->encodeTrailers(response_trailers);
  //  processResponseBodyMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processResponseBodyMessage(*grpc_upstreams_[0], true, [](const HttpBody& body, BodyResponse&) {
    EXPECT_FALSE(body.end_of_stream());
    return true;
  });
  processResponseTrailersMessage(
      *grpc_upstreams_[0], false, [this](const HttpTrailers&, TrailersResponse& trailer_resp) {
        // End result header count 46 + 5 > header count limit 50.
        addMutationSetHeaders(46, *trailer_resp.mutable_header_mutation());
        return true;
      });
  // Prior response headers have already been sent. The stream is reset.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
}

// Test the case that client doesn't send trailer and the  ext_proc filter config
// processing mode is set to send trailer.
TEST_P(ExtProcIntegrationTest, ClientNoTrailerProcessingModeSendTrailer) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  // Client send data with end_stream set to true.
  codec_client_->sendData(*request_encoder_, 10, true);
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    return true;
  });

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test when request trailer is received, it sends out the buffered body to ext_proc server.
TEST_P(ExtProcIntegrationTest, SkipHeaderTrailerSendBodyClientSendAll) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  processRequestBodyMessage(*grpc_upstreams_[0], true, [](const HttpBody& body, BodyResponse&) {
    EXPECT_FALSE(body.end_of_stream());
    return true;
  });
  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendBodyBufferedPartialWithTrailer) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  // Send some data.
  codec_client_->sendData(*request_encoder_, 10, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  processRequestBodyMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendAndReceiveDynamicMetadata) {
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

  // Verify the response received contains the headers from dynamic metadata we expect.
  ASSERT_FALSE((*response).headers().empty());
  auto md_header_result =
      (*response).headers().get(Http::LowerCaseString("receiving_ns_untyped.foo"));
  ASSERT_EQ(1, md_header_result.size());
  EXPECT_EQ("value from ext_proc", md_header_result[0]->value().getStringView());

  verifyDownstreamResponse(*response, 200);
}

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
  EXPECT_THAT(response->headers(), HasNoHeader(Http::LowerCaseString("receiving_ns_untyped.foo")));
  verifyDownstreamResponse(*response, 200);
}

#if defined(USE_CEL_PARSER)
TEST_P(ExtProcIntegrationTest, RequestResponseAttributes) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_request_attributes()->Add("request.path");
  proto_config_.mutable_request_attributes()->Add("request.method");
  proto_config_.mutable_request_attributes()->Add("request.scheme");
  proto_config_.mutable_request_attributes()->Add("request.size");    // tests int64
  proto_config_.mutable_request_attributes()->Add("connection.mtls"); // tests bool
  proto_config_.mutable_request_attributes()->Add("connection.id");   // tests uint64
  proto_config_.mutable_request_attributes()->Add("response.code");
  proto_config_.mutable_response_attributes()->Add("response.code"); // tests int64
  proto_config_.mutable_response_attributes()->Add("response.code_details");

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  // Handle request headers message.
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest& req, ProcessingResponse& resp) {
        // Add something to the response so the message isn't seen as spurious
        envoy::service::ext_proc::v3::HeadersResponse headers_resp;
        *(resp.mutable_request_headers()) = headers_resp;

        EXPECT_TRUE(req.has_request_headers());
        EXPECT_EQ(req.attributes().size(), 1);
        auto proto_struct = req.attributes().at("envoy.filters.http.ext_proc");
        EXPECT_EQ(proto_struct.fields().at("request.path").string_value(), "/");
        EXPECT_EQ(proto_struct.fields().at("request.method").string_value(), "GET");
        EXPECT_EQ(proto_struct.fields().at("request.scheme").string_value(), "http");
        EXPECT_EQ(proto_struct.fields().at("request.size").number_value(), 0);
        EXPECT_EQ(proto_struct.fields().at("connection.mtls").bool_value(), false);
        EXPECT_TRUE(proto_struct.fields().at("connection.id").has_number_value());
        // Make sure we did not include the attribute which was not yet available.
        EXPECT_EQ(proto_struct.fields().size(), 6);
        EXPECT_FALSE(proto_struct.fields().contains("response.code"));

        // Make sure we are not including any data in the deprecated HttpHeaders.attributes.
        EXPECT_TRUE(req.request_headers().attributes().empty());
        return true;
      });

  handleUpstreamRequestWithTrailer();

  // Handle response headers message.
  processGenericMessage(
      *grpc_upstreams_[0], false, [](const ProcessingRequest& req, ProcessingResponse& resp) {
        // Add something to the response so the message isn't seen as spurious
        envoy::service::ext_proc::v3::HeadersResponse headers_resp;
        *(resp.mutable_response_headers()) = headers_resp;

        EXPECT_TRUE(req.has_response_headers());
        EXPECT_EQ(req.attributes().size(), 1);
        auto proto_struct = req.attributes().at("envoy.filters.http.ext_proc");
        EXPECT_EQ(proto_struct.fields().at("response.code").number_value(), 200);
        EXPECT_EQ(proto_struct.fields().at("response.code_details").string_value(),
                  StreamInfo::ResponseCodeDetails::get().ViaUpstream);

        // Make sure we didn't include request attributes in the response-path processing request.
        EXPECT_FALSE(proto_struct.fields().contains("request.method"));

        // Make sure we are not including any data in the deprecated HttpHeaders.attributes.
        EXPECT_TRUE(req.response_headers().attributes().empty());
        return true;
      });

  // Handle response trailers message, making sure we did not send request or response attributes
  // again.
  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest& req, ProcessingResponse& resp) {
                          // Add something to the response so the message isn't seen as spurious
                          envoy::service::ext_proc::v3::TrailersResponse trailer_resp;
                          *(resp.mutable_response_trailers()) = trailer_resp;

                          EXPECT_TRUE(req.has_response_trailers());
                          EXPECT_TRUE(req.attributes().empty());
                          return true;
                        });

  verifyDownstreamResponse(*response, 200);
}
#endif

TEST_P(ExtProcIntegrationTest, GetAndSetHeadersUpstream) {
  ConfigOptions config_option = {};
  config_option.downstream_filter = false;

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
      auto* upstream_codec = old_protocol_options.add_http_filters();
      upstream_codec->set_name("envoy.filters.http.upstream_codec");
      upstream_codec->mutable_typed_config()->PackFrom(
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
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

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

// Test the filter when configured, upon a grpc error, can retry the request and
// get response back.
TEST_P(ExtProcIntegrationTest, RetryOnResponseError) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "Retry is only supported for Envoy gRPC";
  }
  // Set envoy filter timeout to 5s to rule out noise.
  proto_config_.mutable_message_timeout()->set_seconds(5);
  proto_config_.mutable_max_message_timeout()->set_seconds(10);

  envoy::config::core::v3::RetryPolicy* retry_policy =
      proto_config_.mutable_grpc_service()->mutable_retry_policy();
  retry_policy->mutable_num_retries()->set_value(1);
  retry_policy->set_retry_on("resource-exhausted"); // resource-exhausted: 8

  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Fail the stream immediately.
  processor_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "500"}, {"grpc-status", "8"}, {"foo", "bar"}},
      true);
  ASSERT_TRUE(processor_stream_->waitForReset());
  // Retry happens in a new stream.
  ProcessingRequest request_headers_msg2;
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request_headers_msg2));
  EXPECT_TRUE(TestUtility::protoEqual(request_headers_msg2, request_headers_msg));
  processor_stream_->startGrpcStream(false);
  processor_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"foo", "baz"}}, false);
  ProcessingResponse processing_response;
  auto* new_header = processing_response.mutable_request_headers()
                         ->mutable_response()
                         ->mutable_header_mutation()
                         ->add_set_headers();
  new_header->mutable_header()->set_key("foo");
  new_header->mutable_header()->set_raw_value("bluh");
  processor_stream_->sendGrpcMessage(processing_response);
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers()
                .get(Envoy::Http::LowerCaseString("foo"))[0]
                ->value()
                .getStringView(),
            "bluh");
  verifyDownstreamResponse(*response, 200);
}

// Test the filter when configured, retries on a different host in the same
// cluster.
TEST_P(ExtProcIntegrationTest, RetryOnDifferentHost) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "Retry is only supported for Envoy gRPC";
  }
  // Set envoy filter timeout to 5s to rule out noise.
  proto_config_.mutable_message_timeout()->set_seconds(5);
  proto_config_.mutable_max_message_timeout()->set_seconds(10);

  envoy::config::core::v3::RetryPolicy* retry_policy =
      proto_config_.mutable_grpc_service()->mutable_retry_policy();
  retry_policy->mutable_num_retries()->set_value(2);
  retry_policy->set_retry_on("resource-exhausted"); // resource-exhausted: 8
  // Retry on new host only.
  retry_policy->add_retry_host_predicate()->set_name("envoy.retry_host_predicates.previous_hosts");
  // First cluster has 2 endpoints now.
  grpc_upstream_count_ = 3;
  initializeConfig({}, {{0, 2}, {1, 1}});
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  int first_host_id;
  ASSERT_OK_AND_ASSIGN(first_host_id, FakeUpstream::waitForHttpConnection(
                                          *dispatcher_, fake_upstreams_, processor_connection_,
                                          std::chrono::milliseconds(5000)));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request_headers_msg));
  // Fail the stream immediately.
  processor_stream_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "500"}, {"grpc-status", "8"}, {"foo", "bar"}},
      true);
  ASSERT_TRUE(processor_stream_->waitForReset());
  FakeHttpConnectionPtr processor_connection_2;
  int host_id_2;
  ASSERT_OK_AND_ASSIGN(host_id_2, FakeUpstream::waitForHttpConnection(
                                      *dispatcher_, fake_upstreams_, processor_connection_2,
                                      std::chrono::milliseconds(5000)));
  Cleanup close_connection2{[&processor_connection_2]() {
    if (processor_connection_2 != nullptr) {
      ASSERT_TRUE(processor_connection_2->close());
      ASSERT_TRUE(processor_connection_2->waitForDisconnect());
    }
  }};

  // Retry happens on a new host.
  ASSERT_NE(host_id_2, first_host_id);
  FakeStreamPtr processor_stream_2;
  ASSERT_TRUE(processor_connection_2->waitForNewStream(*dispatcher_, processor_stream_2));
  ProcessingRequest request_headers_msg2;
  ASSERT_TRUE(processor_stream_2->waitForGrpcMessage(*dispatcher_, request_headers_msg2));
  processor_stream_2->startGrpcStream(false);
  processor_stream_2->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"foo", "baz"}}, false);
  ProcessingResponse processing_response;
  auto* new_header = processing_response.mutable_request_headers()
                         ->mutable_response()
                         ->mutable_header_mutation()
                         ->add_set_headers();
  new_header->mutable_header()->set_key("foo");
  new_header->mutable_header()->set_raw_value("bluh");
  processor_stream_2->sendGrpcMessage(processing_response);
  processor_stream_2->finishGrpcStream(Grpc::Status::Ok);
  ASSERT_TRUE(processor_stream_2->waitForReset());

  handleUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers()
                .get(Envoy::Http::LowerCaseString("foo"))[0]
                ->value()
                .getStringView(),
            "bluh");
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
        add1->mutable_header()->set_key("x-response-processed");
        add1->mutable_header()->set_raw_value("1");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Header mutation response has been ignored.
  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));

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
        trailer_add->mutable_header()->set_key("x-modified-trailers");
        trailer_add->mutable_header()->set_raw_value("xxx");
        return true;
      });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(*(response->trailers()), HasNoHeader("x-modified-trailers"));

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
  config_option.downstream_filter = false;

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
  config_option.downstream_filter = false;

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

TEST_P(ExtProcIntegrationTest, SidestreamPushbackDownstream) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    return;
  }

  testSidestreamPushbackDownstream(16 * 1024, true);
}

TEST_P(ExtProcIntegrationTest, SidestreamPushbackDownstreamObservabilityMode) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    return;
  }

  proto_config_.set_observability_mode(true);
  testSidestreamPushbackDownstream(16 * 1024, true);
}

TEST_P(ExtProcIntegrationTest, SidestreamPushbackDownstreamRuntimeDisable) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    return;
  }

  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.grpc_side_stream_flow_control", "false"}});

  testSidestreamPushbackDownstream(1030, false);
}

TEST_P(ExtProcIntegrationTest, SidestreamPushbackUpstream) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    return;
  }

  config_helper_.setBufferLimits(1024, 1024);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("hello world", absl::nullopt);

  // Response from upstream server.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(32 * 1024, true);

  std::string body_str = std::string(64 * 1024, 'a');

  bool end_stream = false;
  int count = 0;
  while (!end_stream) {
    processResponseBodyMessage(
        *grpc_upstreams_[0], count == 0 ? true : false,
        [&end_stream, &body_str](const HttpBody& body, BodyResponse& body_resp) {
          end_stream = body.end_of_stream();
          auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
          body_mut->set_body(body_str);
          return true;
        });
    count++;
  }

  // Large body is sent from sidestream server to downstream client. Thus, flow control is expected
  // to be triggered in sidestream cluster.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_"
                                 "paused_reading_total",
                                 1);

  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SidestreamPushbackUpstreamObservabilityMode) {
  if (std::get<1>(std::get<0>(GetParam())) != Envoy::Grpc::ClientType::EnvoyGrpc) {
    return;
  }

  config_helper_.setBufferLimits(1024, 1024);
  proto_config_.set_observability_mode(true);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("hello world", absl::nullopt);

  // Response from upstream server.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(32 * 1024, true);

  std::string body_str = std::string(64 * 1024, 'a');

  bool end_stream = false;
  int count = 0;
  while (!end_stream) {
    processResponseBodyMessage(
        *grpc_upstreams_[0], count == 0 ? true : false,
        [&end_stream, &body_str](const HttpBody& body, BodyResponse& body_resp) {
          end_stream = body.end_of_stream();
          auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
          body_mut->set_body(body_str);
          return true;
        });
    count++;
  }

  // Large body is sent from sidestream server to downstream client. Thus, flow control is expected
  // to be triggered in sidestream cluster.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_"
                                 "paused_reading_total",
                                 2);

  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendBodyBeforeHeaderRespStreamedBasicTest) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  proto_config_.set_send_body_without_waiting_for_header_response(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("hello world", [](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("x-remove-this"), "yes");
  });
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "POST"},      {"host", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        EXPECT_EQ(body.body(), "hello world");
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("replaced body");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), "replaced body");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);
  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });
  processResponseBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendBodyAndTrailerBeforeHeaderRespStreamedMoreDataTest) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SEND);
  proto_config_.set_send_body_without_waiting_for_header_response(true);

  initializeConfig();
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);

  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "hello world", false);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  codec_client_->sendData(*request_encoder_, "foo-bar", true);
  processRequestBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);

  handleUpstreamRequestWithTrailer();
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseTrailersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ServerWaitForBodyBeforeSendsHeaderRespStreamedTest) {
  config_helper_.setBufferLimits(1024, 1024);
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_send_body_without_waiting_for_header_response(true);

  initializeConfig();
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl default_headers;
  HttpTestUtility::addDefaultHeaders(default_headers);

  auto encoder_decoder = codec_client_->startRequest(default_headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  // Downstream client sending 16k data.
  const std::string body_sent(16 * 1024, 's');
  codec_client_->sendData(*request_encoder_, body_sent, true);

  // The ext_proc server receives the headers.
  ProcessingRequest header_request;
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, header_request));
  ASSERT_TRUE(header_request.has_request_headers());

  // The ext_proc server receives 16 chunks of body, each chunk size is 1k.
  std::string body_received;
  bool end_stream = false;
  uint32_t total_body_msg_count = 0;
  while (!end_stream) {
    ProcessingRequest body_request;
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, body_request));
    ASSERT_TRUE(body_request.has_request_body());
    body_received = absl::StrCat(body_received, body_request.request_body().body());
    end_stream = body_request.request_body().end_of_stream();
    total_body_msg_count++;
  }
  EXPECT_TRUE(end_stream);
  EXPECT_EQ(body_received, body_sent);

  // The ext_proc server sends back the header response.
  processor_stream_->startGrpcStream();
  ProcessingResponse response_header;
  auto* header_resp = response_header.mutable_request_headers();
  auto header_mutation = header_resp->mutable_response()->mutable_header_mutation();
  auto* mut = header_mutation->add_set_headers();
  mut->mutable_header()->set_key("x-new-header");
  mut->mutable_header()->set_raw_value("new");
  processor_stream_->sendGrpcMessage(response_header);

  // The ext_proc server sends back the body response.
  const std::string body_upstream(total_body_msg_count, 'r');
  while (total_body_msg_count) {
    ProcessingResponse response_body;
    auto* body_resp = response_body.mutable_request_body();
    auto* body_mut = body_resp->mutable_response()->mutable_body_mutation();
    body_mut->set_body("r");
    processor_stream_->sendGrpcMessage(response_body);
    total_body_msg_count--;
  }

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendBodyBeforeHeaderRespStreamedNotSendTrailerTest) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  proto_config_.set_send_body_without_waiting_for_header_response(true);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBodyAndTrailer("hello world");
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  handleUpstreamRequest(100);
  processResponseHeadersMessage(*grpc_upstreams_[0], false, absl::nullopt);
  processResponseBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, SendHeaderBodyNotSendTrailerTest) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBodyAndTrailer("hello world");
  processRequestHeadersMessage(*grpc_upstreams_[0], true, absl::nullopt);
  processRequestBodyMessage(*grpc_upstreams_[0], false, absl::nullopt);
  handleUpstreamRequest(100);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ModeOverrideAllowed) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_allow_mode_override(true);
  // Configure mode override allow list.
  auto* added_mode = proto_config_.add_allowed_override_modes();
  added_mode->set_request_body_mode(ProcessingMode::NONE);
  added_mode = proto_config_.add_allowed_override_modes();
  added_mode->set_request_body_mode(ProcessingMode::STREAMED);
  added_mode->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = std::string(10, 'a');
  auto response = sendDownstreamRequestWithBody(body_str, absl::nullopt);

  // Process request header message.
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest&, ProcessingResponse& resp) {
        resp.mutable_request_headers();
        resp.mutable_mode_override()->set_response_header_mode(ProcessingMode::SKIP);
        resp.mutable_mode_override()->set_request_body_mode(ProcessingMode::STREAMED);
        return true;
      });

  // ext_proc server will receive the body message since the processing body mode has been
  // overridden from `ProcessingMode::NONE` to `ProcessingMode::STREAMED`
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    return true;
  });
  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ModeOverrideDisallowed) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_allow_mode_override(true);
  // Configure mode override allow list.
  auto* added_mode = proto_config_.add_allowed_override_modes();
  added_mode->set_request_body_mode(ProcessingMode::BUFFERED);
  added_mode->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = std::string(10, 'a');
  auto response = sendDownstreamRequestWithBody(body_str, absl::nullopt);

  // Process request header message.
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest&, ProcessingResponse& resp) {
        resp.mutable_request_headers();
        resp.mutable_mode_override()->set_response_header_mode(ProcessingMode::SKIP);
        resp.mutable_mode_override()->set_request_body_mode(ProcessingMode::NONE);
        return true;
      });

  // ext_proc server still receive the body message even though body mode override was set to
  // ProcessingMode::NONE. It is because that ProcessingMode::NONE was not in allow list.
  processRequestBodyMessage(*grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse&) {
    EXPECT_TRUE(body.end_of_stream());
    return true;
  });
  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

} // namespace Envoy

#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"

#include <chrono>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;

using namespace std::chrono_literals;

// ExtProcIntegrationTest::

void ExtProcIntegrationTest::createUpstreams() {
  HttpIntegrationTest::createUpstreams();
  // Create separate "upstreams" for ExtProc gRPC servers
  for (int i = 0; i < grpc_upstream_count_; ++i) {
    grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
  }
}

void ExtProcIntegrationTest::TearDown() {
  if (processor_connection_) {
    ASSERT_TRUE(processor_connection_->close());
    ASSERT_TRUE(processor_connection_->waitForDisconnect());
  }
  cleanupUpstreamAndDownstream();
}

void ExtProcIntegrationTest::initializeConfig(
    ConfigOptions config_option, const std::vector<std::pair<int, int>>& cluster_endpoints) {
  int total_cluster_endpoints = 0;
  std::for_each(
      cluster_endpoints.begin(), cluster_endpoints.end(),
      [&total_cluster_endpoints](const auto& item) { total_cluster_endpoints += item.second; });
  ASSERT_EQ(total_cluster_endpoints, grpc_upstream_count_);

  config_helper_.addConfigModifier([this, cluster_endpoints, config_option](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
    ConfigHelper::setHttp2(*(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

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
    switch (config_option.filter_setup) {
    case ConfigOptions::FilterSetup::kNone:
      break;
    case ConfigOptions::FilterSetup::kDownstream: {
      // Construct a configuration proto for our filter and then re-write it
      // to JSON so that we can add it to the overall config
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
      ext_proc_filter.set_name(ext_proc_filter_name);
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    } break;
    case ConfigOptions::FilterSetup::kCompositeMatchOnRequestHeaders: {
      envoy::type::matcher::v3::HttpRequestHeaderMatchInput request_match_input;
      request_match_input.set_header_name("match-header");
      prependExtProcCompositeFilter(request_match_input);
    } break;
    case ConfigOptions::FilterSetup::kCompositeMatchOnResponseHeaders: {
      envoy::type::matcher::v3::HttpResponseHeaderMatchInput response_match_input;
      response_match_input.set_header_name("match-header");
      prependExtProcCompositeFilter(response_match_input);
    } break;
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
      Protobuf::Struct test_md_val;
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

    // Add dynamic_metadata_to_headers filter to inject dynamic metadata used for testing
    if (config_option.add_response_processor) {
      simple_filter_config_ =
          std::make_unique<SimpleFilterConfig<DynamicMetadataToHeadersFilter>>();
      registration_ = std::make_unique<
          Envoy::Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>(
          *simple_filter_config_);
      config_helper_.prependFilter(R"EOF(
        name: dynamic-metadata-to-headers-filter
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    )EOF");
    }

    // Add logging test filter only in Envoy gRPC mode.
    // gRPC side stream logging is only supported in Envoy gRPC mode at the moment.
    if (clientType() == Grpc::ClientType::EnvoyGrpc && config_option.add_logging_filter &&
        config_option.valid_grpc_server) {
      LoggingTestFilterConfig logging_filter_config;
      logging_filter_config.set_logging_id(ext_proc_filter_name);
      logging_filter_config.set_upstream_cluster_name(valid_grpc_cluster_name);
      // No need to check the bytes received for observability mode because it is a
      // "send and go" mode.
      logging_filter_config.set_check_received_bytes(!proto_config_.observability_mode());
      logging_filter_config.set_http_rcd("via_upstream");
      if (config_option.logging_filter_config) {
        logging_filter_config.MergeFrom(*config_option.logging_filter_config);
      }

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter logging_filter;
      logging_filter.set_name("logging-test-filter");
      logging_filter.mutable_typed_config()->PackFrom(logging_filter_config);

      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(logging_filter));
    }

    // Parameterize with defer processing to prevent bit rot as filter made
    // assumptions of data flow, prior relying on eager processing.
  });

  if (config_option.add_response_processor) {
    processing_response_factory_ = std::make_unique<TestOnProcessingResponseFactory>();
    processing_response_factory_registration_ =
        std::make_unique<Envoy::Registry::InjectFactory<OnProcessingResponseFactory>>(
            *processing_response_factory_);
    Protobuf::Struct config;
    proto_config_.mutable_on_processing_response()->set_name("test-on-processing-response");
    proto_config_.mutable_on_processing_response()->mutable_typed_config()->PackFrom(config);
  }

  if (config_option.http1_codec) {
    setUpstreamProtocol(Http::CodecType::HTTP1);
    setDownstreamProtocol(Http::CodecType::HTTP1);
  } else {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }
}

void ExtProcIntegrationTest::setPerRouteConfig(Route* route, const ExtProcPerRoute& cfg) {
  Any cfg_any;
  ASSERT_TRUE(cfg_any.PackFrom(cfg));
  route->mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
}

void ExtProcIntegrationTest::setPerHostConfig(VirtualHost& vh, const ExtProcPerRoute& cfg) {
  Any cfg_any;
  ASSERT_TRUE(cfg_any.PackFrom(cfg));
  vh.mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
}

void ExtProcIntegrationTest::protocolConfigEncoding(ProcessingRequest& request) {
  protocol_config_encoded_ = false;
  if (request.has_protocol_config()) {
    protocol_config_encoded_ = true;
    protocol_config_ = request.protocol_config();
  }
}

IntegrationStreamDecoderPtr ExtProcIntegrationTest::sendDownstreamRequest(
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

IntegrationStreamDecoderPtr ExtProcIntegrationTest::sendDownstreamRequestWithBody(
    absl::string_view body,
    absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers,
    bool add_content_length) {
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

IntegrationStreamDecoderPtr
ExtProcIntegrationTest::sendDownstreamRequestWithBodyAndTrailer(absl::string_view body) {
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

void ExtProcIntegrationTest::verifyDownstreamResponse(IntegrationStreamDecoder& response,
                                                      int status_code) {
  ASSERT_TRUE(response.waitForEndStream());
  EXPECT_TRUE(response.complete());
  EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
}

void ExtProcIntegrationTest::handleUpstreamRequest(bool add_content_length, int status_code) {
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", std::to_string(status_code)}};
  uint64_t content_length = 100;
  if (add_content_length) {
    response_headers.setContentLength(content_length);
  }
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(content_length, true);
}

void ExtProcIntegrationTest::verifyChunkedEncoding(
    const Http::RequestOrResponseHeaderMap& headers) {
  EXPECT_EQ(headers.ContentLength(), nullptr);
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().TransferEncoding,
                                      Http::Headers::get().TransferEncodingValues.Chunked));
}

void ExtProcIntegrationTest::handleUpstreamRequestWithTrailer() {
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, false);
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
}

void ExtProcIntegrationTest::handleUpstreamRequestWithResponse(const Buffer::Instance& all_data,
                                                               uint64_t chunk_size) {
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

void ExtProcIntegrationTest::waitForFirstMessage(FakeUpstream& grpc_upstream,
                                                 ProcessingRequest& request) {
  ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
}

void ExtProcIntegrationTest::processGenericMessage(
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

void ExtProcIntegrationTest::processRequestHeadersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_request_headers());
  protocolConfigEncoding(request);
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

void ExtProcIntegrationTest::processRequestTrailersMessage(
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

void ExtProcIntegrationTest::processResponseHeadersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_response_headers());
  protocolConfigEncoding(request);

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

void ExtProcIntegrationTest::processRequestBodyMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb,
    bool check_downstream_flow_control) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_request_body());
  protocolConfigEncoding(request);

  if (first_message) {
    processor_stream_->startGrpcStream();
  }

  if (check_downstream_flow_control) {
    // Check the flow control counter in downstream, which is triggered on the request
    // path to ext_proc server (i.e., from side stream).
    test_server_->waitForCounterGe("http.config_test.downstream_flow_control_paused_reading_total",
                                   1);
  }

  // Send back the response from ext_proc server.
  ProcessingResponse response;
  auto* body = response.mutable_request_body();
  const bool sendReply = !cb || (*cb)(request.request_body(), *body);
  if (sendReply) {
    processor_stream_->sendGrpcMessage(response);
  }
}

void ExtProcIntegrationTest::processResponseBodyMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_response_body());
  protocolConfigEncoding(request);

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

void ExtProcIntegrationTest::processResponseTrailersMessage(
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

void ExtProcIntegrationTest::processAndRespondImmediately(
    FakeUpstream& grpc_upstream, bool first_message,
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
void ExtProcIntegrationTest::serverSendNewTimeout(const uint64_t timeout_ms) {
  ProcessingResponse response;
  if (timeout_ms < 1000) {
    response.mutable_override_message_timeout()->set_nanos(timeout_ms * 1000000);
  } else {
    response.mutable_override_message_timeout()->set_seconds(timeout_ms / 1000);
  }
  processor_stream_->sendGrpcMessage(response);
}

void ExtProcIntegrationTest::newTimeoutWrongConfigTest(const uint64_t timeout_ms) {
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

void ExtProcIntegrationTest::addMutationSetHeaders(
    const int count, envoy::service::ext_proc::v3::HeaderMutation& mutation) {
  for (int i = 0; i < count; i++) {
    auto* headers = mutation.add_set_headers();
    auto str = absl::StrCat("x-test-header-internal-", std::to_string(i));
    headers->mutable_append()->set_value(false);
    headers->mutable_header()->set_key(str);
    headers->mutable_header()->set_raw_value(str);
  }
}

void ExtProcIntegrationTest::testWithHeaderMutation(ConfigOptions config_option) {
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_append()->set_value(false);
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
void ExtProcIntegrationTest::testWithoutHeaderMutation(ConfigOptions config_option) {
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

void ExtProcIntegrationTest::addMutationRemoveHeaders(
    const int count, envoy::service::ext_proc::v3::HeaderMutation& mutation) {
  for (int i = 0; i < count; i++) {
    mutation.add_remove_headers(absl::StrCat("x-test-header-internal-", std::to_string(i)));
  }
}

void ExtProcIntegrationTest::testGetAndFailStream() {
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
  verifyDownstreamResponse(*response, 500);
}

void ExtProcIntegrationTest::testGetAndCloseStream() {
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

void ExtProcIntegrationTest::testSendDyanmicMetadata() {
  Protobuf::Struct test_md_struct;
  (*test_md_struct.mutable_fields())["foo"].set_string_value("value from ext_proc");

  Protobuf::Value md_val;
  *(md_val.mutable_struct_value()) = test_md_struct;

  processGenericMessage(
      *grpc_upstreams_[0], true, [md_val](const ProcessingRequest& req, ProcessingResponse& resp) {
        // Verify the processing request contains the untyped metadata we injected.
        EXPECT_TRUE(req.metadata_context().filter_metadata().contains("forwarding_ns_untyped"));
        const Protobuf::Struct& fwd_metadata =
            req.metadata_context().filter_metadata().at("forwarding_ns_untyped");
        EXPECT_EQ(1, fwd_metadata.fields_size());
        EXPECT_TRUE(fwd_metadata.fields().contains("foo"));
        EXPECT_EQ("value from set_metadata", fwd_metadata.fields().at("foo").string_value());

        // Verify the processing request contains the typed metadata we injected.
        EXPECT_TRUE(req.metadata_context().typed_filter_metadata().contains("forwarding_ns_typed"));
        const Protobuf::Any& fwd_typed_metadata =
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

void ExtProcIntegrationTest::testSidestreamPushbackDownstream(uint32_t body_size,
                                                              bool check_downstream_flow_control) {
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

void ExtProcIntegrationTest::initializeConfigDuplexStreamed(bool both_direction) {
  config_helper_.setBufferLimits(1024, 1024);
  auto* processing_mode = proto_config_.mutable_processing_mode();
  processing_mode->set_request_header_mode(ProcessingMode::SEND);
  processing_mode->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
  processing_mode->set_request_trailer_mode(ProcessingMode::SEND);
  if (!both_direction) {
    processing_mode->set_response_header_mode(ProcessingMode::SKIP);
  } else {
    processing_mode->set_response_header_mode(ProcessingMode::SEND);
    processing_mode->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_response_trailer_mode(ProcessingMode::SEND);
  }

  initializeConfig();
  HttpIntegrationTest::initialize();
}

IntegrationStreamDecoderPtr
ExtProcIntegrationTest::initAndSendDataDuplexStreamedMode(absl::string_view body_sent,
                                                          bool end_of_stream, bool both_direction) {
  initializeConfigDuplexStreamed(both_direction);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl default_headers;
  HttpTestUtility::addDefaultHeaders(default_headers);

  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> encoder_decoder =
      codec_client_->startRequest(default_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, body_sent, end_of_stream);
  return response;
}

void ExtProcIntegrationTest::serverReceiveHeaderDuplexStreamed(ProcessingRequest& header,
                                                               bool first_message, bool response) {
  if (first_message) {
    EXPECT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_));
    EXPECT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  EXPECT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, header));
  if (response) {
    EXPECT_TRUE(header.has_response_headers());
  } else {
    EXPECT_TRUE(header.has_request_headers());
  }
}

uint32_t ExtProcIntegrationTest::serverReceiveBodyDuplexStreamed(absl::string_view body_sent,
                                                                 bool response, bool compare_body) {
  std::string body_received;
  bool end_stream = false;
  uint32_t total_req_body_msg = 0;
  while (!end_stream) {
    ProcessingRequest body_request;
    EXPECT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, body_request));
    if (response) {
      EXPECT_TRUE(body_request.has_response_body());
      body_received = absl::StrCat(body_received, body_request.response_body().body());
      end_stream = body_request.response_body().end_of_stream();
    } else {
      EXPECT_TRUE(body_request.has_request_body());
      body_received = absl::StrCat(body_received, body_request.request_body().body());
      end_stream = body_request.request_body().end_of_stream();
    }
    total_req_body_msg++;
  }
  EXPECT_TRUE(end_stream);
  if (compare_body) {
    EXPECT_EQ(body_received, body_sent);
  }
  return total_req_body_msg;
}

void ExtProcIntegrationTest::serverSendHeaderRespDuplexStreamed(bool first_message, bool response) {
  if (first_message) {
    processor_stream_->startGrpcStream();
  }
  ProcessingResponse response_header;
  HeadersResponse* header_resp;
  if (response) {
    header_resp = response_header.mutable_response_headers();
  } else {
    header_resp = response_header.mutable_request_headers();
  }
  auto* header_mutation = header_resp->mutable_response()->mutable_header_mutation();
  auto* sh = header_mutation->add_set_headers();
  auto* header = sh->mutable_header();
  sh->mutable_append()->set_value(false);
  header->set_key("x-new-header");
  header->set_raw_value("new");
  processor_stream_->sendGrpcMessage(response_header);
}

void ExtProcIntegrationTest::serverSendBodyRespDuplexStreamed(uint32_t total_resp_body_msg,
                                                              bool end_of_stream, bool response,
                                                              absl::string_view body_sent) {
  for (uint32_t i = 0; i < total_resp_body_msg; i++) {
    ProcessingResponse response_body;
    BodyResponse* body_resp;
    if (response) {
      body_resp = response_body.mutable_response_body();
    } else {
      body_resp = response_body.mutable_request_body();
    }

    auto* body_mut = body_resp->mutable_response()->mutable_body_mutation();
    auto* streamed_response = body_mut->mutable_streamed_response();
    if (!body_sent.empty()) {
      streamed_response->set_body(body_sent);
    } else {
      streamed_response->set_body("r");
    }
    if (end_of_stream) {
      const bool end_of_stream = (i == total_resp_body_msg - 1) ? true : false;
      streamed_response->set_end_of_stream(end_of_stream);
    }
    processor_stream_->sendGrpcMessage(response_body);
  }
}

void ExtProcIntegrationTest::serverSendTrailerRespDuplexStreamed() {
  ProcessingResponse response_trailer;
  auto* trailer_resp = response_trailer.mutable_request_trailers()->mutable_header_mutation();
  auto* sh = trailer_resp->add_set_headers();
  sh->mutable_append()->set_value(false);
  auto* header = sh->mutable_header();
  header->set_key("x-new-trailer");
  header->set_raw_value("new");
  processor_stream_->sendGrpcMessage(response_trailer);
}

void ExtProcIntegrationTest::prependExtProcCompositeFilter(const Protobuf::Message& match_input) {
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter composite_filter;
  composite_filter.set_name("composite");

  envoy::extensions::common::matching::v3::ExtensionWithMatcher extension_with_matcher;
  auto* extension_config = extension_with_matcher.mutable_extension_config();
  extension_config->set_name("composite");
  envoy::extensions::filters::http::composite::v3::Composite composite_config;
  extension_config->mutable_typed_config()->PackFrom(composite_config);

  auto* matcher_tree = extension_with_matcher.mutable_xds_matcher()->mutable_matcher_tree();
  auto* input = matcher_tree->mutable_input();
  input->set_name("match-input");
  input->mutable_typed_config()->PackFrom(match_input);

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction execute_filter_action;
  auto* typed_config = execute_filter_action.mutable_typed_config();
  typed_config->set_name("envoy.filters.http.ext_proc");
  typed_config->mutable_typed_config()->PackFrom(proto_config_);

  auto& on_match = (*matcher_tree->mutable_exact_match_map()->mutable_map())["match"];
  on_match.mutable_action()->set_name("composite-action");
  on_match.mutable_action()->mutable_typed_config()->PackFrom(execute_filter_action);

  composite_filter.mutable_typed_config()->PackFrom(extension_with_matcher);
  config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(composite_filter),
                               true);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

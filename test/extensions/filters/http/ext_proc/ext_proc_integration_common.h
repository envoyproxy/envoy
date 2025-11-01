#pragma once

#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::ProtocolConfiguration;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::TestOnProcessingResponseFactory;
using test::integration::filters::LoggingTestFilterConfig;

struct ConfigOptions {
  enum class FilterSetup {
    kNone,
    kDownstream,
    kCompositeMatchOnRequestHeaders,
    kCompositeMatchOnResponseHeaders,
  };

  FilterSetup filter_setup = FilterSetup::kDownstream;
  bool valid_grpc_server = true;
  bool add_logging_filter = false;
  absl::optional<LoggingTestFilterConfig> logging_filter_config = absl::nullopt;
  bool http1_codec = false;
  bool add_metadata = false;
  bool add_response_processor = false;
};

// A filter that sticks dynamic metadata info into headers for integration testing.
class DynamicMetadataToHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "dynamic-metadata-to-headers-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    if (decoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata_size() > 0) {
      const auto& md = decoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
      for (const auto& md_entry : md) {
        std::string key_prefix = md_entry.first;
        for (const auto& field : md_entry.second.fields()) {
          headers.addCopy(Http::LowerCaseString(key_prefix), field.first);
        }
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

// These tests exercise the ext_proc filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class ExtProcIntegrationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTest {
protected:
  ExtProcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override;

  void TearDown() override;

  void initializeConfig(ConfigOptions config_option = {},
                        const std::vector<std::pair<int, int>>& cluster_endpoints = {{0, 1},
                                                                                     {1, 1}});

  bool IsEnvoyGrpc() { return std::get<1>(GetParam()) == Envoy::Grpc::ClientType::EnvoyGrpc; }

  void setPerRouteConfig(Route* route, const ExtProcPerRoute& cfg);
  void setPerHostConfig(VirtualHost& vh, const ExtProcPerRoute& cfg);
  void protocolConfigEncoding(ProcessingRequest& request);

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers);
  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers,
      bool add_content_length = false);
  IntegrationStreamDecoderPtr sendDownstreamRequestWithBodyAndTrailer(absl::string_view body);

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code);
  void handleUpstreamRequest(bool add_content_length = false, int status_code = 200);
  void verifyChunkedEncoding(const Http::RequestOrResponseHeaderMap& headers);
  void handleUpstreamRequestWithTrailer();
  void handleUpstreamRequestWithResponse(const Buffer::Instance& all_data, uint64_t chunk_size);

  void waitForFirstMessage(FakeUpstream& grpc_upstream, ProcessingRequest& request);

  void processGenericMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const ProcessingRequest&, ProcessingResponse&)>> cb);
  void processRequestHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb);
  void processRequestTrailersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb);
  void processResponseHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb);
  void
  processRequestBodyMessage(FakeUpstream& grpc_upstream, bool first_message,
                            absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb,
                            bool check_downstream_flow_control = false);
  void processResponseBodyMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb);
  void processResponseTrailersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb);
  void processAndRespondImmediately(FakeUpstream& grpc_upstream, bool first_message,
                                    absl::optional<std::function<void(ImmediateResponse&)>> cb);

  // ext_proc server sends back a response to tell Envoy to stop the
  // original timer and start a new timer.
  void serverSendNewTimeout(const uint64_t timeout_ms);

  // The new timeout message is ignored by Envoy due to different reasons, like
  // new_timeout setting is out-of-range, or max_message_timeout is not configured.
  void newTimeoutWrongConfigTest(const uint64_t timeout_ms);

  void addMutationSetHeaders(const int count,
                             envoy::service::ext_proc::v3::HeaderMutation& mutation);

  // Verify content-length header set by external processor is removed and chunked encoding is
  // enabled.
  void testWithHeaderMutation(ConfigOptions config_option);

  // Verify existing content-length header (i.e., no external processor mutation) is removed and
  // chunked encoding is enabled.
  void testWithoutHeaderMutation(ConfigOptions config_option);

  void addMutationRemoveHeaders(const int count,
                                envoy::service::ext_proc::v3::HeaderMutation& mutation);

  void testGetAndFailStream();
  void testGetAndCloseStream();
  void testSendDyanmicMetadata();
  void testSidestreamPushbackDownstream(uint32_t body_size, bool check_downstream_flow_control);
  void initializeConfigDuplexStreamed(bool both_direction = false);

  IntegrationStreamDecoderPtr initAndSendDataDuplexStreamedMode(absl::string_view body_sent,
                                                                bool end_of_stream,
                                                                bool both_direction = false);

  void serverReceiveHeaderDuplexStreamed(ProcessingRequest& header, bool first_message = true,
                                         bool response = false);
  uint32_t serverReceiveBodyDuplexStreamed(absl::string_view body_sent, bool response = false,
                                           bool compare_body = true);
  void serverSendHeaderRespDuplexStreamed(bool first_message = true, bool response = false);
  void serverSendBodyRespDuplexStreamed(uint32_t total_resp_body_msg, bool end_of_stream = true,
                                        bool response = false, absl::string_view body_sent = "");
  void serverSendTrailerRespDuplexStreamed();
  void prependExtProcCompositeFilter(const Protobuf::Message& match_input);

  std::unique_ptr<SimpleFilterConfig<DynamicMetadataToHeadersFilter>> simple_filter_config_;
  std::unique_ptr<
      Envoy::Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>
      registration_;
  std::unique_ptr<TestOnProcessingResponseFactory> processing_response_factory_;
  std::unique_ptr<Envoy::Registry::InjectFactory<OnProcessingResponseFactory>>
      processing_response_factory_registration_;
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  bool protocol_config_encoded_ = false;
  ProtocolConfiguration protocol_config_{};
  uint32_t max_message_timeout_ms_{0};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
  TestScopedRuntime scoped_runtime_;
  // Number of grpc upstreams in the test.
  int grpc_upstream_count_ = 2;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

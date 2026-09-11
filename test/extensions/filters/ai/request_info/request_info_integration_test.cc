#include <string>

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/integration/http_integration.h"

#include "absl/synchronization/notification.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace {

class RequestInfoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  RequestInfoIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override { setDownstreamProtocol(Http::CodecType::HTTP2); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  void initializeWithExtProc() {
    // Observability mode: the processor records the request-headers message, never responds.
    test_processor_.start(
        GetParam(),
        [this](grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                                        envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
          envoy::service::ext_proc::v3::ProcessingRequest request;
          while (stream->Read(&request)) {
            if (!request.has_request_headers() || headers_seen_.HasBeenNotified()) {
              continue;
            }
            const auto& typed_metadata = request.metadata_context().typed_filter_metadata();
            const auto entry = typed_metadata.find("envoy.ai.request_info");
            record_present_ = entry != typed_metadata.end();
            if (record_present_) {
              ASSERT_TRUE(entry->second.UnpackTo(&captured_));
            }
            headers_seen_.Notify();
          }
        });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* processor_cluster = bootstrap.mutable_static_resources()->add_clusters();
      processor_cluster->set_name("ext_proc_server");
      processor_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");
      auto* address = processor_cluster->mutable_load_assignment()
                          ->add_endpoints()
                          ->add_lb_endpoints()
                          ->mutable_endpoint()
                          ->mutable_address()
                          ->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
      address->set_port_value(test_processor_.port());
      ConfigHelper::setHttp2(*processor_cluster);
    });

    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_api_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
    });

    // The manager is prepended ahead of ext_proc so it publishes before ext_proc sends headers.
    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc;
    ext_proc.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    ext_proc.set_observability_mode(true);
    ext_proc.mutable_processing_mode()->set_request_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
    ext_proc.mutable_processing_mode()->set_response_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
    ext_proc.mutable_metadata_options()->mutable_forwarding_namespaces()->add_typed(
        "envoy.ai.request_info");
    envoy::config::listener::v3::Filter ext_proc_filter;
    ext_proc_filter.set_name("envoy.filters.http.ext_proc");
    std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(ext_proc);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));

    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {}
  filters:
  - name: envoy.filters.ai.request_info
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.ai.request_info.v3.RequestInfo
)EOF");

    initialize();
  }

  uint64_t counterValue(absl::string_view suffix) {
    for (const auto& counter : test_server_->counters()) {
      if (absl::EndsWith(counter->name(), suffix)) {
        return counter->value();
      }
    }
    return 0;
  }

  Extensions::HttpFilters::ExternalProcessing::TestProcessor test_processor_;
  absl::Notification headers_seen_;
  bool record_present_{false};
  envoy::data::ai::v3::RequestInfo captured_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RequestInfoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RequestInfoIntegrationTest, PublishesRecordBeforeReleasingHeaders) {
  initializeWithExtProc();

  const std::string payload =
      R"({"model":"gpt-4o","stream":false,"max_completion_tokens":64,)"
      R"("messages":[{"role":"user","content":"hi"}],)"
      R"("tools":[{"type":"function","function":{"name":"lookup","parameters":{}}}]})";
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/v1/chat/completions"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-type", "application/json"}},
      payload);

  waitForNextUpstreamRequest();
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()),
            nlohmann::json::parse(payload));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  ASSERT_TRUE(headers_seen_.WaitForNotificationWithTimeout(absl::Seconds(5)));
  ASSERT_TRUE(record_present_);
  EXPECT_EQ(captured_.api_protocol(), envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  EXPECT_EQ(captured_.model(), "gpt-4o");
  ASSERT_TRUE(captured_.has_stream());
  EXPECT_FALSE(captured_.stream().value());
  EXPECT_EQ(captured_.max_output_tokens().value(), 64);
  EXPECT_EQ(captured_.message_count().value(), 1);
  EXPECT_EQ(captured_.tool_count().value(), 1);
  EXPECT_EQ(counterValue("ai_protocol_manager.request_info.published"), 1);
  EXPECT_EQ(counterValue("ai_protocol_manager.request_info.partial"), 0);
}

} // namespace
} // namespace Envoy

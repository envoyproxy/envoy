// Integration tests verifying that substitution formatter commands in a gRPC service's
// `initial_metadata` work when the gRPC client is created on a worker thread. Some commands create
// state that can only be created on the main thread when they are parsed, so the metadata must not
// be parsed on the worker:
// - envoy.formatter.file_content, declared in the service's `formatters`, creates a thread local
//   slot and a file watcher for each %FILE_CONTENT()%.
// - The built-in %CEL()% command, which needs no `formatters`, gets its expression builder from
//   the singleton manager.
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

constexpr absl::string_view FileName = "grpc_service_initial_metadata.txt";
constexpr absl::string_view MetadataKey = "x-test-metadata";

class GrpcServiceInitialMetadataIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                  public HttpIntegrationTest {
public:
  GrpcServiceInitialMetadataIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  // Configures the gRPC service's initial metadata with `metadata_value`, and, if
  // `file_content_formatter` is set, declares envoy.formatter.file_content in its `formatters`.
  void initializeWithMetadata(const std::string& metadata_value, bool file_content_formatter) {
    config_helper_.addConfigModifier([this, metadata_value, file_content_formatter](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* grpc_cluster = bootstrap.mutable_static_resources()->add_clusters();
      grpc_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      grpc_cluster->set_name("grpc_cluster");
      ConfigHelper::setHttp2(*grpc_cluster);

      test::integration::filters::ServerFactoryContextFilterConfigDual filter_config;
      auto* grpc_service = filter_config.mutable_grpc_service();
      setGrpcService(*grpc_service, "grpc_cluster", fake_upstreams_.back()->localAddress());
      if (file_content_formatter) {
        auto* formatter = grpc_service->add_formatters();
        formatter->set_name("envoy.formatter.file_content");
        std::ignore = formatter->mutable_typed_config()->PackFrom(
            envoy::extensions::formatter::file_content::v3::FileContent());
      }
      auto* metadata = grpc_service->add_initial_metadata();
      metadata->set_key(std::string(MetadataKey));
      metadata->set_value(metadata_value);

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
      filter.set_name("server-factory-context-filter-dual");
      std::ignore = filter.mutable_typed_config()->PackFrom(filter_config);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(filter));
    });

    HttpIntegrationTest::initialize();
  }

  void TearDown() override {
    if (fake_grpc_connection_ != nullptr) {
      AssertionResult result = fake_grpc_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_grpc_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_grpc_connection_.reset();
    }
    cleanupUpstreamAndDownstream();
  }

  // Sends a request through the filter, answers its gRPC call, and returns the value of the
  // MetadataKey initial metadata sent by the gRPC client ("" if it is absent).
  std::string sendRequestAndGetMetadata() {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    if (fake_grpc_connection_ == nullptr) {
      AssertionResult result =
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_grpc_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    FakeStreamPtr grpc_request;
    AssertionResult result = fake_grpc_connection_->waitForNewStream(*dispatcher_, grpc_request);
    RELEASE_ASSERT(result, result.message());
    helloworld::HelloRequest hello_request;
    result = grpc_request->waitForGrpcMessage(*dispatcher_, hello_request);
    RELEASE_ASSERT(result, result.message());

    std::string value;
    const auto entries = grpc_request->headers().get(Http::LowerCaseString(MetadataKey));
    if (!entries.empty()) {
      value = std::string(entries[0]->value().getStringView());
    }

    // The filter continues the request once it receives a reply.
    grpc_request->startGrpcStream();
    helloworld::HelloReply reply;
    reply.set_message("ack");
    grpc_request->sendGrpcMessage(reply);

    waitForNextUpstreamRequest(0);
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    grpc_request->finishGrpcStream(Grpc::Status::Ok);
    RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");
    EXPECT_EQ("200", response->headers().getStatusValue());
    return value;
  }

  FakeHttpConnectionPtr fake_grpc_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, GrpcServiceInitialMetadataIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// The gRPC client is created on the worker thread handling the first request. Its
// %FILE_CONTENT()% initial metadata must resolve to the file's content, and follow updates to it.
TEST_P(GrpcServiceInitialMetadataIntegrationTest, FileContentInInitialMetadata) {
  TestEnvironment::writeStringToFileForTest(std::string(FileName), "initial-content");
  initializeWithMetadata(
      fmt::format("%FILE_CONTENT({})%", TestEnvironment::temporaryPath(FileName)), true);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  EXPECT_EQ("initial-content", sendRequestAndGetMetadata());

  // The file_content DataSourceProvider watches for Modified events, so a direct write triggers
  // the re-read. Send requests until the update propagates.
  TestEnvironment::writeStringToFileForTest(std::string(FileName), "rotated-content");
  while (sendRequestAndGetMetadata() != "rotated-content") {
    absl::SleepFor(absl::Milliseconds(10));
  }

  codec_client_->close();
}

#if defined(USE_CEL_PARSER)
// The built-in %CEL()% command needs no `formatters`. The gRPC client is created on the worker
// thread handling the first request, and its initial metadata must resolve the expression.
TEST_P(GrpcServiceInitialMetadataIntegrationTest, CelInInitialMetadataWithoutFormatters) {
  initializeWithMetadata("%CEL('cel-value')%", false);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  EXPECT_EQ("cel-value", sendRequestAndGetMetadata());

  codec_client_->close();
}
#endif

} // namespace
} // namespace Envoy

// Integration tests verifying that substitution formatter commands in a gRPC service's
// `initial_metadata` work when the gRPC client is created on a worker thread. Some commands create
// state that can only be created on the main thread when they are parsed, so the metadata must not
// be parsed on the worker:
// - envoy.formatter.file_content, declared in the service's `formatters`, creates a thread local
//   slot and a file watcher for each %FILE_CONTENT()%.
// - The built-in %CEL()% command, which needs no `formatters`, gets its expression builder from
//   the singleton manager.
// Formatters that load secrets over SDS must also work when the filter config arrives after the
// listener has warmed, by LDS or ECDS.
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"
#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

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
constexpr absl::string_view SdsFileName = "grpc_service_initial_metadata_sds.yaml";
constexpr absl::string_view EcdsFileName = "grpc_service_initial_metadata_ecds.json";
constexpr absl::string_view EcdsFilterName = "ecds-filter";
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

  // Adds the cluster for the gRPC service.
  static void addGrpcCluster(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* grpc_cluster = bootstrap.mutable_static_resources()->add_clusters();
    grpc_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    grpc_cluster->set_name("grpc_cluster");
    ConfigHelper::setHttp2(*grpc_cluster);
  }

  // Returns the test filter's proto config. Its gRPC service sends `metadata_value` as the
  // MetadataKey initial metadata, and declares `formatters`.
  test::integration::filters::ServerFactoryContextFilterConfigDual
  filterProtoConfig(const std::string& metadata_value,
                    const std::vector<envoy::config::core::v3::TypedExtensionConfig>& formatters) {
    test::integration::filters::ServerFactoryContextFilterConfigDual filter_config;
    auto* grpc_service = filter_config.mutable_grpc_service();
    setGrpcService(*grpc_service, "grpc_cluster", fake_upstreams_.back()->localAddress());
    for (const auto& formatter : formatters) {
      *grpc_service->add_formatters() = formatter;
    }
    auto* metadata = grpc_service->add_initial_metadata();
    metadata->set_key(std::string(MetadataKey));
    metadata->set_value(metadata_value);
    return filter_config;
  }

  // Returns the test filter's config (see filterProtoConfig()).
  std::string
  filterConfig(const std::string& metadata_value,
               const std::vector<envoy::config::core::v3::TypedExtensionConfig>& formatters) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
    filter.set_name("server-factory-context-filter-dual");
    std::ignore =
        filter.mutable_typed_config()->PackFrom(filterProtoConfig(metadata_value, formatters));
    return MessageUtil::getJsonStringFromMessageOrError(filter);
  }

  // Initializes with the test filter (see filterConfig()).
  void initializeWithMetadata(
      const std::string& metadata_value,
      const std::vector<envoy::config::core::v3::TypedExtensionConfig>& formatters) {
    config_helper_.addConfigModifier(
        [this, metadata_value, formatters](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          addGrpcCluster(bootstrap);
          config_helper_.prependFilter(filterConfig(metadata_value, formatters));
        });
    HttpIntegrationTest::initialize();
  }

  static envoy::config::core::v3::TypedExtensionConfig fileContentFormatter() {
    envoy::config::core::v3::TypedExtensionConfig formatter;
    formatter.set_name("envoy.formatter.file_content");
    std::ignore = formatter.mutable_typed_config()->PackFrom(
        envoy::extensions::formatter::file_content::v3::FileContent());
    return formatter;
  }

  // Writes the SDS file, which contains the secret `token` = "sds-token".
  static void writeSdsSecret() {
    TestEnvironment::writeStringToFileForTest(std::string(SdsFileName), R"EOF(
resources:
- "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
  name: token
  generic_secret:
    secret:
      inline_string: "sds-token"
)EOF");
  }

  // Returns a generic_secret formatter that loads the secret `token` from the SDS file.
  static envoy::config::core::v3::TypedExtensionConfig sdsSecretFormatter() {
    envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret;
    auto& secret_config = (*generic_secret.mutable_secret_configs())["token"];
    secret_config.set_name("token");
    secret_config.mutable_sds_config()->mutable_path_config_source()->set_path(
        TestEnvironment::temporaryPath(SdsFileName));
    envoy::config::core::v3::TypedExtensionConfig formatter;
    formatter.set_name("envoy.formatter.generic_secret");
    std::ignore = formatter.mutable_typed_config()->PackFrom(generic_secret);
    return formatter;
  }

  // Returns the config of an HTTP filter named EcdsFilterName, whose config is loaded by ECDS from
  // the ECDS file.
  static std::string ecdsFilterConfig() {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
    filter.set_name(std::string(EcdsFilterName));
    auto* config_discovery = filter.mutable_config_discovery();
    config_discovery->mutable_config_source()->mutable_path_config_source()->set_path(
        TestEnvironment::temporaryPath(EcdsFileName));
    config_discovery->add_type_urls(
        "type.googleapis.com/test.integration.filters.ServerFactoryContextFilterConfigDual");
    return MessageUtil::getJsonStringFromMessageOrError(filter);
  }

  // Writes the ECDS file, which contains the test filter's config (see filterProtoConfig()). The
  // file is moved into place, which the filesystem subscription watches for.
  void writeEcdsFilterConfig(
      const std::string& version, const std::string& metadata_value,
      const std::vector<envoy::config::core::v3::TypedExtensionConfig>& formatters) {
    envoy::config::core::v3::TypedExtensionConfig filter_config;
    filter_config.set_name(std::string(EcdsFilterName));
    std::ignore = filter_config.mutable_typed_config()->PackFrom(
        filterProtoConfig(metadata_value, formatters));
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    std::ignore = response.add_resources()->PackFrom(filter_config);

    const std::string temp_file_name = absl::StrCat(EcdsFileName, ".tmp");
    TestEnvironment::writeStringToFileForTest(
        temp_file_name, MessageUtil::getJsonStringFromMessageOrError(response));
    TestEnvironment::renameFile(TestEnvironment::temporaryPath(temp_file_name),
                                TestEnvironment::temporaryPath(EcdsFileName));
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
      fmt::format("%FILE_CONTENT({})%", TestEnvironment::temporaryPath(FileName)),
      {fileContentFormatter()});
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
  initializeWithMetadata("%CEL('cel-value')%", {});
  codec_client_ = makeHttpConnection(lookupPort("http"));

  EXPECT_EQ("cel-value", sendRequestAndGetMetadata());

  codec_client_->close();
}
#endif

// A filter added by an LDS update after the server has initialized, whose gRPC service's initial
// metadata uses a secret loaded over SDS, must load the secret.
TEST_P(GrpcServiceInitialMetadataIntegrationTest, SdsSecretInInitialMetadataAfterLdsUpdate) {
  writeSdsSecret();
  config_helper_.addConfigModifier(
      [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) { addGrpcCluster(bootstrap); });
  HttpIntegrationTest::initialize();

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.prependFilter(filterConfig("%SECRET(token)%", {sdsSecretFormatter()}));
  new_config_helper.setLds("1");
  test_server_->waitForCounter("listener_manager.lds.update_success", testing::Ge(2));
  test_server_->waitForGauge("listener_manager.total_listeners_warming", testing::Eq(0));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_EQ("sds-token", sendRequestAndGetMetadata());

  codec_client_->close();
}

// A filter config delivered by ECDS after the listener has warmed, whose gRPC service's initial
// metadata uses a secret loaded over SDS, must load the secret.
TEST_P(GrpcServiceInitialMetadataIntegrationTest, SdsSecretInInitialMetadataAfterEcdsUpdate) {
  writeSdsSecret();
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    addGrpcCluster(bootstrap);
    writeEcdsFilterConfig("1", "static-value", {});
    config_helper_.prependFilter(ecdsFilterConfig());
  });
  HttpIntegrationTest::initialize();
  const std::string config_reload_counter =
      absl::StrCat("extension_config_discovery.http_filter.", EcdsFilterName, ".config_reload");
  test_server_->waitForCounter(config_reload_counter, testing::Ge(1));

  writeEcdsFilterConfig("2", "%SECRET(token)%", {sdsSecretFormatter()});
  test_server_->waitForCounter(config_reload_counter, testing::Ge(2));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_EQ("sds-token", sendRequestAndGetMetadata());

  codec_client_->close();
}

} // namespace
} // namespace Envoy

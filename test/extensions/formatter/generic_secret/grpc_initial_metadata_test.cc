// Test verifying that a formatter extension declared in GrpcService.formatters can resolve a
// substitution formatter command (%SECRET()%) in the gRPC initial metadata header values.

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"

#include "source/common/grpc/async_client_impl.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {
namespace {

class GrpcInitialMetadataFormatterTest : public testing::Test {
public:
  GrpcInitialMetadataFormatterTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {}

  // Starts a stream on grpc_client_ and returns the value of the "authorization" initial-metadata
  // header that would be sent upstream, or "" if the header is absent.
  std::string startStreamAndGetAuthHeader() {
    NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
    Http::AsyncClient::StreamCallbacks* http_callbacks = nullptr;
    StreamInfo::StreamInfoImpl stream_info{context_.time_system_, nullptr,
                                           StreamInfo::FilterState::LifeSpan::FilterChain};
    NiceMock<Http::MockAsyncClientStream> http_stream;
    ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
    EXPECT_CALL(http_client_, start(_, _))
        .WillOnce(Invoke([&](Http::AsyncClient::StreamCallbacks& callbacks,
                             const Http::AsyncClient::StreamOptions&) {
          http_callbacks = &callbacks;
          return &http_stream;
        }));
    EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
    std::string auth_value;
    EXPECT_CALL(http_stream, sendHeaders(_, _))
        .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) {
          const auto entry = headers.get(Http::LowerCaseString("authorization"));
          if (!entry.empty()) {
            auth_value = std::string(entry[0]->value().getStringView());
          }
          http_callbacks->onReset();
        }));
    auto stream = grpc_client_->start(*method_descriptor_, grpc_callbacks,
                                      Http::AsyncClient::StreamOptions());
    EXPECT_EQ(stream, nullptr);
    return auth_value;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Upstream::MockClusterManager>& cm_{context_.cluster_manager_};
  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
};

// A %SECRET()% command in the initial metadata is resolved via the generic_secret formatter
// extension declared in `formatters`, and the resolved value is sent as a header.
TEST_F(GrpcInitialMetadataFormatterTest, InitialMetadataResolvedFromFormatterExtension) {
  // Register a static generic secret that the formatter will resolve.
  context_.resetSecretManager();
  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("api-token");
  secret.mutable_generic_secret()->mutable_secret()->set_inline_string("s3cret-value");
  ASSERT_TRUE(context_.secretManager().addStaticSecret(secret).ok());

  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");

  auto* initial_metadata_entry = config.mutable_initial_metadata()->Add();
  initial_metadata_entry->set_key("authorization");
  initial_metadata_entry->set_value("Bearer %SECRET(api-token)%");

  auto* formatter = config.mutable_formatters()->Add();
  formatter->set_name("envoy.formatter.generic_secret");
  envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
  (*generic_secret_cfg.mutable_secret_configs())["api-token"].set_name("api-token");
  ASSERT_TRUE(formatter->mutable_typed_config()->PackFrom(generic_secret_cfg));

  auto formatters = parseGrpcServiceInitialMetadataForServer(config, context_);
  ASSERT_TRUE(formatters.ok());
  grpc_client_ = *AsyncClientImpl::create(config, context_, *formatters);
  cm_.initializeThreadLocalClusters({"test_cluster"});
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  EXPECT_EQ("Bearer s3cret-value", startStreamAndGetAuthHeader());
}

// Validates the SDS/init path: a secret sourced from an ``sds_config`` registers an init target
// with the init manager of the context passed to parseGrpcServiceInitialMetadata(). The secret
// resolves to empty (header omitted) until initialization delivers it, after which it appears in
// the initial metadata.
TEST_F(GrpcInitialMetadataFormatterTest, SdsBackedSecretResolvesAfterInitialization) {
  context_.resetSecretManager();

  // Capture the init target the generic_secret SDS subscription registers with the init manager.
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(context_.init_manager_, add(_))
      .WillOnce(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));

  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");

  auto* initial_metadata_entry = config.mutable_initial_metadata()->Add();
  initial_metadata_entry->set_key("authorization");
  initial_metadata_entry->set_value("%SECRET(api-token)%");

  auto* formatter = config.mutable_formatters()->Add();
  formatter->set_name("envoy.formatter.generic_secret");
  envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
  auto& secret_config = (*generic_secret_cfg.mutable_secret_configs())["api-token"];
  secret_config.set_name("api-token");
  secret_config.mutable_sds_config()->mutable_ads();
  ASSERT_TRUE(formatter->mutable_typed_config()->PackFrom(generic_secret_cfg));

  Server::GenericFactoryContextImpl owner_context(context_, context_.messageValidationVisitor());
  auto formatters = parseGrpcServiceInitialMetadata(config, owner_context);
  ASSERT_TRUE(formatters.ok());
  grpc_client_ = *AsyncClientImpl::create(config, context_, *formatters);
  cm_.initializeThreadLocalClusters({"test_cluster"});
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  // Before initialization, the secret is empty, so the substitution yields an empty value and the
  // header is omitted.
  EXPECT_EQ("", startStreamAndGetAuthHeader());

  // Drive initialization and deliver the secret over SDS.
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  init_target_handle->initialize(init_watcher);
  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("api-token");
  secret.mutable_generic_secret()->mutable_secret()->set_inline_string("s3cret-value");
  const auto decoded_resources = TestUtility::decodeResources({secret});
  EXPECT_TRUE(
      cm_.subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "v1").ok());

  // After the secret loads, it resolves in the initial metadata.
  EXPECT_EQ("s3cret-value", startStreamAndGetAuthHeader());
}

} // namespace
} // namespace Grpc
} // namespace Envoy

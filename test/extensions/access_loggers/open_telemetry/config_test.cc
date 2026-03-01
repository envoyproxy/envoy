#include "envoy/access_log/access_log_config.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"

#include "source/extensions/access_loggers/open_telemetry/access_log_impl.h"
#include "source/extensions/access_loggers/open_telemetry/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {
namespace {

class OpenTelemetryAccessLogConfigTest : public testing::Test {
public:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<Envoy::AccessLog::AccessLogInstanceFactory>::getFactory(
        "envoy.access_loggers.open_telemetry");
    ASSERT_NE(nullptr, factory_);

    message_ = factory_->createEmptyConfigProto();
    ASSERT_NE(nullptr, message_);
  }

  // Helper to set up gRPC config and expectations using top-level fields.
  void setupGrpcConfig() {
    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
                factoryForGrpcService(_, _, _))
        .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
        }));

    access_log_config_.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("bar");
    access_log_config_.set_log_name("foo");
    TestUtility::jsonConvert(access_log_config_, *message_);
  }

  ::Envoy::AccessLog::FilterPtr filter_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig
      access_log_config_;
  ProtobufTypes::MessagePtr message_;
  Envoy::AccessLog::AccessLogInstanceFactory* factory_{};
};

// Verifies gRPC transport configuration creates a valid access log instance.
TEST_F(OpenTelemetryAccessLogConfigTest, GrpcConfigOk) {
  setupGrpcConfig();
  ::Envoy::AccessLog::InstanceSharedPtr instance =
      factory_->createAccessLogInstance(*message_, std::move(filter_), context_);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog*>(instance.get()));
}

// Verifies HTTP transport configuration creates a valid access log instance.
TEST_F(OpenTelemetryAccessLogConfigTest, HttpConfigOk) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  auto* http_service = config.mutable_http_service();
  http_service->mutable_http_uri()->set_uri("http://localhost:4318/v1/logs");
  http_service->mutable_http_uri()->set_cluster("otel_collector");
  http_service->mutable_http_uri()->mutable_timeout()->set_seconds(1);

  ProtobufTypes::MessagePtr http_message = factory_->createEmptyConfigProto();
  TestUtility::jsonConvert(config, *http_message);

  ::Envoy::AccessLog::InstanceSharedPtr instance =
      factory_->createAccessLogInstance(*http_message, std::move(filter_), context_);
  EXPECT_NE(nullptr, instance);
}

// Verifies top-level grpc_service configuration creates a valid access log instance.
TEST_F(OpenTelemetryAccessLogConfigTest, TopLevelGrpcServiceConfigOk) {
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
              factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("otel_collector");
  config.set_log_name("my_access_log");

  ProtobufTypes::MessagePtr grpc_message = factory_->createEmptyConfigProto();
  TestUtility::jsonConvert(config, *grpc_message);

  ::Envoy::AccessLog::InstanceSharedPtr instance =
      factory_->createAccessLogInstance(*grpc_message, std::move(filter_), context_);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog*>(instance.get()));
}

// Verifies that configuring both gRPC and HTTP transport throws an exception.
TEST_F(OpenTelemetryAccessLogConfigTest, BothGrpcAndHttpConfigFails) {
  // Set up gRPC config using top-level field.
  access_log_config_.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("bar");
  access_log_config_.set_log_name("foo");

  // Also add HTTP config - this should cause rejection.
  auto* http_service = access_log_config_.mutable_http_service();
  http_service->mutable_http_uri()->set_uri("http://localhost:4318/v1/logs");
  http_service->mutable_http_uri()->set_cluster("otel_collector");
  http_service->mutable_http_uri()->mutable_timeout()->set_seconds(1);

  ProtobufTypes::MessagePtr both_message = factory_->createEmptyConfigProto();
  TestUtility::jsonConvert(access_log_config_, *both_message);

  EXPECT_THROW_WITH_MESSAGE(
      factory_->createAccessLogInstance(*both_message, std::move(filter_), context_),
      EnvoyException,
      "OpenTelemetry access logger can only have one transport configured. "
      "Specify exactly one of: grpc_service, http_service, or common_config.grpc_service.");
}

// Verifies that missing transport config throws an exception.
TEST_F(OpenTelemetryAccessLogConfigTest, NoTransportConfigFails) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.set_log_name("my_access_log");
  // No transport configured.

  ProtobufTypes::MessagePtr no_transport_message = factory_->createEmptyConfigProto();
  TestUtility::jsonConvert(config, *no_transport_message);

  EXPECT_THROW_WITH_MESSAGE(
      factory_->createAccessLogInstance(*no_transport_message, std::move(filter_), context_),
      EnvoyException,
      "OpenTelemetry access logger requires one of: grpc_service, http_service, or "
      "common_config.grpc_service to be configured.");
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

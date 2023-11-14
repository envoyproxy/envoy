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

    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
                factoryForGrpcService(_, _, _))
        .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
        }));

    auto* common_config = access_log_config_.mutable_common_config();
    common_config->set_log_name("foo");
    common_config->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("bar");
    common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    TestUtility::jsonConvert(access_log_config_, *message_);
  }

  ::Envoy::AccessLog::FilterPtr filter_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig
      access_log_config_;
  ProtobufTypes::MessagePtr message_;
  Envoy::AccessLog::AccessLogInstanceFactory* factory_{};
};

// Normal OK configuration.
TEST_F(OpenTelemetryAccessLogConfigTest, Ok) {
  ::Envoy::AccessLog::InstanceSharedPtr instance =
      factory_->createAccessLogInstance(*message_, std::move(filter_), context_);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog*>(instance.get()));
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

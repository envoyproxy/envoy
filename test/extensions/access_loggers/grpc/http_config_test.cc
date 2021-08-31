#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/access_log_config.h"
#include "envoy/stats/scope.h"

#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {
namespace {

class HttpGrpcAccessLogConfigTest : public testing::Test {
public:
  void SetUp() override {
    factory_ =
        Registry::FactoryRegistry<Server::Configuration::AccessLogInstanceFactory>::getFactory(
            "envoy.access_loggers.http_grpc");
    ASSERT_NE(nullptr, factory_);

    message_ = factory_->createEmptyConfigProto();
    ASSERT_NE(nullptr, message_);
  }

  void run(const std::string cluster_name) {
    const auto fake_static = "fake_static";
    const auto fake_added_via_api = "fake_added_via_api";
    context_.cluster_manager_.initializeClusters({fake_static, fake_added_via_api}, {});
    ON_CALL(*context_.cluster_manager_.active_clusters_[fake_static]->info_, addedViaApi())
        .WillByDefault(Return(false));
    ON_CALL(*context_.cluster_manager_.active_clusters_[fake_added_via_api]->info_, addedViaApi())
        .WillByDefault(Return(true));

    auto* common_config = http_grpc_access_log_.mutable_common_config();
    common_config->set_log_name("foo");
    common_config->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(cluster_name);
    common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    TestUtility::jsonConvert(http_grpc_access_log_, *message_);

    if (cluster_name == fake_static) {
      EXPECT_CALL(context_.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
          .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
            return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
          }));
      AccessLog::InstanceSharedPtr instance =
          factory_->createAccessLogInstance(*message_, std::move(filter_), context_);
      EXPECT_NE(nullptr, instance);
      EXPECT_NE(nullptr, dynamic_cast<HttpGrpcAccessLog*>(instance.get()));
    } else if (cluster_name == fake_added_via_api) {
      EXPECT_THROW_WITH_MESSAGE(
          factory_->createAccessLogInstance(*message_, std::move(filter_), context_),
          EnvoyException, fmt::format("gRPC client cluster '{}' is not static", cluster_name));
    } else {
      EXPECT_THROW_WITH_MESSAGE(
          factory_->createAccessLogInstance(*message_, std::move(filter_), context_),
          EnvoyException, fmt::format("Unknown gRPC client cluster '{}'", cluster_name));
    }
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig http_grpc_access_log_;
  ProtobufTypes::MessagePtr message_;
  Server::Configuration::AccessLogInstanceFactory* factory_{};
};

// Normal OK configuration.
TEST_F(HttpGrpcAccessLogConfigTest, Ok) { run("fake_static"); }

// Wrong configuration with invalid clusters.
TEST_F(HttpGrpcAccessLogConfigTest, InvalidCluster) {
  run("fake_added_via_api");
  run("non_exist");
}

} // namespace
} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

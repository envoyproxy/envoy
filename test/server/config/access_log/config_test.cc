#include "envoy/registry/registry.h"
#include "envoy/server/access_log_config.h"

#include "common/access_log/grpc_access_log_impl.h"
#include "common/config/well_known_names.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

class HttpGrpcAccessLogConfigTest : public testing::Test {
public:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<AccessLogInstanceFactory>::getFactory(
        Config::AccessLogNames::get().HTTP_GRPC);
    ASSERT_NE(nullptr, factory_);

    message_ = factory_->createEmptyConfigProto();
    ASSERT_NE(nullptr, message_);

    EXPECT_CALL(context_.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _))
        .WillOnce(Invoke([](const envoy::api::v2::GrpcService&, Stats::Scope&) {
          return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
        }));

    auto* common_config = http_grpc_access_log_.mutable_common_config();
    common_config->set_log_name("foo");
    common_config->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("bar");
    MessageUtil::jsonConvert(http_grpc_access_log_, *message_);
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::api::v2::filter::accesslog::HttpGrpcAccessLogConfig http_grpc_access_log_;
  ProtobufTypes::MessagePtr message_;
  AccessLogInstanceFactory* factory_{};
};

// Normal OK configuration.
TEST_F(HttpGrpcAccessLogConfigTest, Ok) {
  AccessLog::InstanceSharedPtr instance =
      factory_->createAccessLogInstance(*message_, std::move(filter_), context_);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog::HttpGrpcAccessLog*>(instance.get()));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

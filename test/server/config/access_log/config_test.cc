#include "envoy/registry/registry.h"
#include "envoy/server/access_log_config.h"

#include "common/access_log/grpc_access_log_impl.h"
#include "common/config/well_known_names.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(AccessLogConfigTest, HttpGrpcAccessLogTest) {
  auto factory = Registry::FactoryRegistry<AccessLogInstanceFactory>::getFactory(
      Config::AccessLogNames::get().HTTP_GRPC);
  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  envoy::api::v2::filter::accesslog::HttpGrpcAccessLogConfig http_grpc_access_log;
  auto* common_config = http_grpc_access_log.mutable_common_config();
  common_config->set_log_name("foo");
  common_config->set_cluster_name("bar");
  MessageUtil::jsonConvert(http_grpc_access_log, *message);

  AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(*message, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog::HttpGrpcAccessLog*>(instance.get()));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy

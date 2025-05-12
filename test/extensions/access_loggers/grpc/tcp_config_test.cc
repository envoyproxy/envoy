#include "envoy/access_log/access_log_config.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"

#include "source/extensions/access_loggers/grpc/tcp_grpc_access_log_impl.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace TcpGrpc {
namespace {

class TcpGrpcAccessLogConfigTest : public testing::Test {
public:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
        "envoy.access_loggers.tcp_grpc");
    ASSERT_NE(nullptr, factory_);

    message_ = factory_->createEmptyConfigProto();
    ASSERT_NE(nullptr, message_);
  }

  void run(const std::string cluster_name) {
    const auto good_cluster = "good_cluster";

    auto* common_config = tcp_grpc_access_log_.mutable_common_config();
    common_config->set_log_name("foo");
    common_config->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(cluster_name);
    common_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    TestUtility::jsonConvert(tcp_grpc_access_log_, *message_);

    if (cluster_name == good_cluster) {
      EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
                  factoryForGrpcService(_, _, _))
          .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
            return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
          }));
      AccessLog::InstanceSharedPtr instance =
          factory_->createAccessLogInstance(*message_, std::move(filter_), context_);
      EXPECT_NE(nullptr, instance);
      EXPECT_NE(nullptr, dynamic_cast<TcpGrpcAccessLog*>(instance.get()));
    } else {
      EXPECT_THROW_WITH_MESSAGE(
          factory_->createAccessLogInstance(*message_, std::move(filter_), context_),
          EnvoyException, "fake");
    }
  }

  AccessLog::FilterPtr filter_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig tcp_grpc_access_log_;
  ProtobufTypes::MessagePtr message_;
  AccessLog::AccessLogInstanceFactory* factory_{};
};

// Normal OK configuration.
TEST_F(TcpGrpcAccessLogConfigTest, Ok) { run("good_cluster"); }

class MockGrpcAccessLoggerCache : public GrpcCommon::GrpcAccessLoggerCache {
public:
  // GrpcAccessLoggerCache
  MOCK_METHOD(GrpcCommon::GrpcAccessLoggerSharedPtr, getOrCreateLogger,
              (const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               Common::GrpcAccessLoggerType logger_type));
};

// Test for the issue described in https://github.com/envoyproxy/envoy/pull/18081
TEST(TcpGrpcAccessLog, TlsLifetimeCheck) {
  NiceMock<ThreadLocal::MockInstance> tls;
  Stats::IsolatedStoreImpl scope;
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache{new MockGrpcAccessLoggerCache()};
  tls.defer_data_ = true;
  {
    AccessLog::MockFilter* filter{new NiceMock<AccessLog::MockFilter>()};
    envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig config;
    config.mutable_common_config()->set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    EXPECT_CALL(*logger_cache, getOrCreateLogger(_, _))
        .WillOnce([](const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
                         common_config,
                     Common::GrpcAccessLoggerType type) {
          // This is a part of the actual getOrCreateLogger code path and shouldn't crash.
          std::make_pair(MessageUtil::hash(common_config), type);
          return nullptr;
        });
    // Set tls callback in the TcpGrpcAccessLog constructor,
    // but it is not called yet since we have defer_data_ = true.
    const auto access_log =
        std::make_unique<TcpGrpcAccessLog>(AccessLog::FilterPtr{filter}, config, tls, logger_cache);
    // Intentionally make access_log die earlier in this scope to simulate the situation where the
    // creator has been deleted yet the tls callback is not called yet.
  }
  // Verify the tls callback does not crash since it captures the env with proper lifetime.
  tls.call();
}

} // namespace
} // namespace TcpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/redis_command_stats.h"

#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

using testing::An;
using testing::Return;

class AwsIamAuthenticatorTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
    TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);
    // Tue Jan  2 03:04:05 UTC 2018
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Event::SimulatedTimeSystem time_system_;
  envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config;
};

TEST_F(AwsIamAuthenticatorTest, NormalAuthentication) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);
  aws_iam_config.set_region("region");
  aws_iam_config.set_cache_name("cachename");
  aws_iam_config.set_service_name("elasticache");

  auto aws_iam_authenticator =
      AwsIamAuthenticatorFactory::initAwsIamAuthenticator(context_, aws_iam_config);

  auto token = aws_iam_authenticator->getAuthToken("test");
  EXPECT_EQ(
      token,
      "cachename/"
      "?Action=connect&User=test&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=akid%2F20180102%"
      "2Fregion%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-Expires=60&X-Amz-"
      "Security-Token=token&X-Amz-Signature="
      "3bf9d8841acb6db28373efcab8b9ccf1076a7a9ab39faf489002fa0555a1f89c&X-Amz-SignedHeaders=host");
}

class MockAwsIamAuthenticator : public AwsIamAuthenticatorBase {
public:
  ~MockAwsIamAuthenticator() override { ENVOY_LOG_MISC(debug, "destructor called"); };
  MOCK_METHOD(std::string, getAuthToken, (std::string auth_user));
  MOCK_METHOD(bool, addCallbackIfCredentialsPending,
              (Extensions::Common::Aws::CredentialsPendingCallback && cb));
};

// Verify filter decodeData functionality when credentials are pending.
TEST_F(AwsIamAuthenticatorTest, CredentialPendingAuthentication) {
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats;
  NiceMock<Stats::MockIsolatedStatsStore> stats;
  std::shared_ptr<Upstream::MockHost> host{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher;
  auto config = std::make_shared<Client::ConfigImpl>(Client::createConnPoolSettings());

  Envoy::Extensions::Common::Aws::CredentialsPendingCallback capture;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();

  EXPECT_CALL(*host, createConnection_(_, _)).WillOnce(Return(conn_info));

  redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(stats.symbolTable());
  Envoy::Extensions::NetworkFilters::Common::Redis::Client::ClientFactoryImpl factory;
  auto mock_authenticator = std::make_shared<MockAwsIamAuthenticator>();
  EXPECT_CALL(dispatcher, createTimer_(_)).Times(2);
  EXPECT_CALL(*mock_authenticator, getAuthToken("username")).WillOnce(Return("auth_token"));
  EXPECT_CALL(*mock_authenticator,
              addCallbackIfCredentialsPending(
                  An<Envoy::Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(testing::DoAll(testing::SaveArg<0>(&capture), testing::Return(true)));

  Envoy::Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client = factory.create(
      host, dispatcher, config, redis_command_stats, *stats.rootScope(), "username", "password",
      false,
      absl::optional<Envoy::Extensions::NetworkFilters::Common::Redis::AwsIamAuthenticator::
                         AwsIamAuthenticatorSharedPtr>(mock_authenticator));

  capture();
  client->close();
}

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

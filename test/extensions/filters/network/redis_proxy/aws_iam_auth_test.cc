#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/redis_command_stats.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace AwsIamAuthenticator {

using testing::An;
using testing::InSequence;
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
  envoy::extensions::filters::network::redis_proxy::v3::AwsIam aws_iam_config_;
};

TEST_F(AwsIamAuthenticatorTest, NormalAuthentication) {
  aws_iam_config_.set_region("region");
  aws_iam_config_.set_cache_name("cachename");
  aws_iam_config_.set_service_name("elasticache");
  const auto& aws_iam_config = aws_iam_config_;
  auto aws_iam_authenticator =
      AwsIamAuthenticatorFactory::initAwsIamAuthenticator(context_, aws_iam_config);

  auto token = aws_iam_authenticator.value()->getAuthToken("test", aws_iam_config);
  EXPECT_EQ(
      token,
      "cachename/"
      "?Action=connect&User=test&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=akid%2F20180102%"
      "2Fregion%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-Expires=60&X-Amz-"
      "Security-Token=token&X-Amz-Signature="
      "3bf9d8841acb6db28373efcab8b9ccf1076a7a9ab39faf489002fa0555a1f89c&X-Amz-SignedHeaders=host");
}

TEST_F(AwsIamAuthenticatorTest, HasCredentialFileProvider) {
  aws_iam_config_.set_region("region");
  aws_iam_config_.set_cache_name("cachename");
  aws_iam_config_.set_service_name("elasticache");
  aws_iam_config_.mutable_credential_provider()->mutable_credentials_file_provider();
  const auto& aws_iam_config = aws_iam_config_;
  auto aws_iam_authenticator =
      AwsIamAuthenticatorFactory::initAwsIamAuthenticator(context_, aws_iam_config);

  auto token = aws_iam_authenticator.value()->getAuthToken("test", aws_iam_config);
  EXPECT_EQ(
      token,
      "cachename/"
      "?Action=connect&User=test&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=akid%2F20180102%"
      "2Fregion%2Felasticache%2Faws4_request&X-Amz-Date=20180102T030405Z&X-Amz-Expires=60&X-Amz-"
      "Security-Token=token&X-Amz-Signature="
      "3bf9d8841acb6db28373efcab8b9ccf1076a7a9ab39faf489002fa0555a1f89c&X-Amz-SignedHeaders=host");
}

TEST_F(AwsIamAuthenticatorTest, HasCustomChainButNoProviders) {
  aws_iam_config_.set_cache_name("cachename");
  aws_iam_config_.set_service_name("elasticache");
  aws_iam_config_.mutable_credential_provider()->set_custom_credential_provider_chain("true");
  const auto& aws_iam_config = aws_iam_config_;
  auto aws_iam_authenticator =
      AwsIamAuthenticatorFactory::initAwsIamAuthenticator(context_, aws_iam_config);
  EXPECT_FALSE(aws_iam_authenticator.has_value());
}

// Verify filter correctly pauses requests when credentials are pending.
TEST_F(AwsIamAuthenticatorTest, CredentialPendingAuthentication) {
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats;
  NiceMock<Stats::MockIsolatedStatsStore> stats;
  std::shared_ptr<Upstream::MockHost> host{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher;
  auto config = std::make_shared<Client::ConfigImpl>(Client::createConnPoolSettings());

  Envoy::Extensions::Common::Aws::CredentialsPendingCallback capture;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  auto mock_connection = new NiceMock<Network::MockClientConnection>();
  conn_info.connection_ = mock_connection;
  aws_iam_config_.set_region("region");
  aws_iam_config_.set_cache_name("cachename");
  aws_iam_config_.set_service_name("elasticache");
  const auto aws_iam_config = aws_iam_config_;
  EXPECT_CALL(*host, createConnection_(_, _)).WillOnce(Return(conn_info));

  redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(stats.symbolTable());
  Envoy::Extensions::NetworkFilters::Common::Redis::Client::ClientFactoryImpl factory;
  auto signer = std::make_unique<Extensions::Common::Aws::MockSigner>();

  auto mock_authenticator =
      std::make_shared<AwsIamAuthenticator::MockAwsIamAuthenticator>(std::move(signer));
  absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr> authenticator =
      mock_authenticator;

  EXPECT_CALL(dispatcher, createTimer_(_)).Times(2);
  EXPECT_CALL(*mock_authenticator, getAuthToken("username", _)).WillOnce(Return("auth_token"));
  EXPECT_CALL(*mock_authenticator,
              addCallbackIfCredentialsPending(
                  An<Envoy::Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(testing::DoAll(testing::SaveArg<0>(&capture), testing::Return(true)));
  // We should get a write from the auth command
  EXPECT_CALL(*mock_connection, write(_, _)).Times(0);
  Envoy::Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client =
      factory.create(host, dispatcher, config, redis_command_stats, *stats.rootScope(), "username",
                     "password", false, aws_iam_config, authenticator);

  Common::Redis::RespValue request1;
  Client::MockClientCallbacks callbacks;
  // Add a request and it should be buffered, until the capture callback is called which will
  // disable queue and flush
  Client::PoolRequest* handle1 = client->makeRequest(request1, callbacks);
  EXPECT_NE(nullptr, handle1);
  // One write for AUTH command, one write for buffer
  InSequence s;

  // Auth is 45 bytes
  EXPECT_CALL(*mock_connection, write(testing::Property(&Buffer::OwnedImpl::length, 45), false));
  // RespValue is 5 bytes
  EXPECT_CALL(*mock_connection, write(testing::Property(&Buffer::OwnedImpl::length, 5), false));
  // Handle callback for close
  EXPECT_CALL(callbacks, onFailure());

  capture();
  client->close();
}

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

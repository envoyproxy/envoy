#include <cstdlib>

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/google_grpc_creds_impl.h"

#include "test/common/grpc/utility.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

// In general, below, we force execution of all paths, but because the
// underlying grpc::{CallCredentials,ChannelCredentials} don't have any real way
// of getting at the underlying state, we can at best just make sure we don't
// crash, compare with nullptr and/or look at vector lengths.

class CredsUtilityTest : public testing::Test {
public:
  CredsUtilityTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
};

TEST_F(CredsUtilityTest, GetChannelCredentials) {
  EXPECT_EQ(nullptr, CredsUtility::getChannelCredentials({}, *api_));
  envoy::config::core::v3::GrpcService::GoogleGrpc config;
  auto* creds = config.mutable_channel_credentials();
  EXPECT_EQ(nullptr, CredsUtility::getChannelCredentials(config, *api_));
  creds->mutable_ssl_credentials();
  EXPECT_NE(nullptr, CredsUtility::getChannelCredentials(config, *api_));
  creds->mutable_local_credentials();
  EXPECT_NE(nullptr, CredsUtility::getChannelCredentials(config, *api_));

  const std::string var_name = "GOOGLE_APPLICATION_CREDENTIALS";
  EXPECT_EQ(nullptr, ::getenv(var_name.c_str()));
  const std::string creds_path = TestEnvironment::runfilesPath("test/common/grpc/service_key.json");
  TestEnvironment::setEnvVar(var_name, creds_path, 0);
  creds->mutable_google_default();
  EXPECT_NE(nullptr, CredsUtility::getChannelCredentials(config, *api_));
  TestEnvironment::unsetEnvVar(var_name);
}

TEST_F(CredsUtilityTest, DefaultSslChannelCredentials) {
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials({}, *api_));
  envoy::config::core::v3::GrpcService config;
  auto* creds = config.mutable_google_grpc()->mutable_channel_credentials();
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials(config, *api_));
  creds->mutable_ssl_credentials();
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials(config, *api_));
}

TEST_F(CredsUtilityTest, CallCredentials) {
  EXPECT_TRUE(CredsUtility::callCredentials({}).empty());
  {
    // Invalid refresh token doesn't crash and gets elided.
    envoy::config::core::v3::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_google_refresh_token("invalid");
    EXPECT_TRUE(CredsUtility::callCredentials(config).empty());
  }
  {
    // Singleton access token succeeds.
    envoy::config::core::v3::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_access_token("foo");
    EXPECT_EQ(1, CredsUtility::callCredentials(config).size());
  }
  {
    // Multiple call credentials.
    envoy::config::core::v3::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_access_token("foo");
    config.add_call_credentials()->mutable_google_compute_engine();
    EXPECT_EQ(2, CredsUtility::callCredentials(config).size());
  }
  // The full set of call credentials are evaluated below in
  // CredsUtility.DefaultChannelCredentials.
}

TEST_F(CredsUtilityTest, DefaultChannelCredentials) {
  { EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials({}, *api_)); }
  {
    envoy::config::core::v3::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials(config, *api_));
  }
  {
    envoy::config::core::v3::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->add_call_credentials()->set_access_token("foo");
    google_grpc->add_call_credentials()->mutable_google_compute_engine();
    google_grpc->add_call_credentials()->set_google_refresh_token(R"EOF(
    {
      "client_id": "123",
      "client_secret": "foo",
      "refresh_token": "bar",
      "type": "authorized_user"
    }
    )EOF");
    {
      auto* service_account_jwt_access =
          google_grpc->add_call_credentials()->mutable_service_account_jwt_access();
      service_account_jwt_access->set_json_key(R"EOF(
      {
        "private_key": "foo",
        "private_key_id": "bar",
        "client_id": "123",
        "client_email": "foo@bar",
        "type": "service_account"
      }
      )EOF");
      service_account_jwt_access->set_token_lifetime_seconds(123);
    }
    {
      auto* google_iam = google_grpc->add_call_credentials()->mutable_google_iam();
      google_iam->set_authorization_token("foo");
      google_iam->set_authority_selector("bar");
    }
    // Should be ignored..
    google_grpc->add_call_credentials()->mutable_from_plugin()->set_name("foo");
    EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials(config, *api_));
  }
  {
    envoy::config::core::v3::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    auto* sts_service = config.mutable_google_grpc()->add_call_credentials()->mutable_sts_service();
    sts_service->set_token_exchange_service_uri("http://tokenexchangeservice.com");
    sts_service->set_subject_token_path("/var/run/example_token");
    sts_service->set_subject_token_type("urn:ietf:params:oauth:token-type:access_token");
    EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials(config, *api_));
  }
}

} // namespace
} // namespace Grpc
} // namespace Envoy

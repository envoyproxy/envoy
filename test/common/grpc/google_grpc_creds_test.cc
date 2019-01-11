#include "common/grpc/google_grpc_creds_impl.h"

#include "test/common/grpc/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

// In general, below, we force execution of all paths, but because the
// underlying grpc::{CallCredentials,ChannelCredentials} don't have any real way
// of getting at the underlying state, we can at best just make sure we don't
// crash, compare with nullptr and/or look at vector lengths.

TEST(CredsUtility, GetChannelCredentials) {
  EXPECT_EQ(nullptr, CredsUtility::getChannelCredentials({}));
  envoy::api::v2::core::GrpcService::GoogleGrpc config;
  auto* creds = config.mutable_channel_credentials();
  EXPECT_EQ(nullptr, CredsUtility::getChannelCredentials(config));
  creds->mutable_ssl_credentials();
  EXPECT_NE(nullptr, CredsUtility::getChannelCredentials(config));
  creds->mutable_local_credentials();
  EXPECT_NE(nullptr, CredsUtility::getChannelCredentials(config));
}

TEST(CredsUtility, DefaultSslChannelCredentials) {
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials({}));
  envoy::api::v2::core::GrpcService config;
  auto* creds = config.mutable_google_grpc()->mutable_channel_credentials();
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials(config));
  creds->mutable_ssl_credentials();
  EXPECT_NE(nullptr, CredsUtility::defaultSslChannelCredentials(config));
}

TEST(CredsUtility, CallCredentials) {
  EXPECT_TRUE(CredsUtility::callCredentials({}).empty());
  {
    // Invalid refresh token doesn't crash and gets elided.
    envoy::api::v2::core::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_google_refresh_token("invalid");
    EXPECT_TRUE(CredsUtility::callCredentials(config).empty());
  }
  {
    // Singleton access token succeeds.
    envoy::api::v2::core::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_access_token("foo");
    EXPECT_EQ(1, CredsUtility::callCredentials(config).size());
  }
  {
    // Multiple call credentials.
    envoy::api::v2::core::GrpcService::GoogleGrpc config;
    config.add_call_credentials()->set_access_token("foo");
    config.add_call_credentials()->mutable_google_compute_engine();
    EXPECT_EQ(2, CredsUtility::callCredentials(config).size());
  }
  // The full set of call credentials are evaluated below in
  // CredsUtility.DefaultChannelCredentials.
}

TEST(CredsUtility, DefaultChannelCredentials) {
  { EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials({})); }
  {
    envoy::api::v2::core::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials(config));
  }
  {
    envoy::api::v2::core::GrpcService config;
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
    EXPECT_NE(nullptr, CredsUtility::defaultChannelCredentials(config));
  }
}

} // namespace
} // namespace Grpc
} // namespace Envoy

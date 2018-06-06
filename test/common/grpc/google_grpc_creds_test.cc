#include "common/grpc/google_grpc_creds_impl.h"

#include "test/common/grpc/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

// Force execution of all paths in Grpc::defaultChannelCredentials(). Since the underlying
// grpc::{CallCredentials,ChannelCredentials} don't have any real way of getting at the underlying
// state, we can at best just make sure we don't crash.
TEST(DefaultChannelCredentials, All) {
  { EXPECT_NE(nullptr, defaultChannelCredentials({})); }
  { EXPECT_NE(nullptr, defaultChannelCredentials({}, true)); }
  {
    envoy::api::v2::core::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    EXPECT_NE(nullptr, defaultChannelCredentials(config));
  }
  {
    envoy::api::v2::core::GrpcService config;
    TestUtility::setTestSslGoogleGrpcConfig(config, true);
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->add_call_credentials()->set_access_token("foo");
    google_grpc->add_call_credentials()->mutable_google_compute_engine();
    google_grpc->add_call_credentials()->set_google_refresh_token("bar");
    {
      auto* service_account_jwt_access =
          google_grpc->add_call_credentials()->mutable_service_account_jwt_access();
      service_account_jwt_access->set_json_key("foo");
      service_account_jwt_access->set_token_lifetime_seconds(123);
    }
    {
      auto* google_iam = google_grpc->add_call_credentials()->mutable_google_iam();
      google_iam->set_authorization_token("foo");
      google_iam->set_authority_selector("bar");
    }
    // Should be ignored..
    google_grpc->add_call_credentials()->mutable_from_plugin()->set_name("foo");
    EXPECT_NE(nullptr, defaultChannelCredentials(config));
  }
}

} // namespace
} // namespace Grpc
} // namespace Envoy

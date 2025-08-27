#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::MockFunction;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

TEST(Credentials, Default) {
  const auto c = Credentials();
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllNull) {
  const auto c = Credentials({}, {}, {});
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllEmpty) {
  const auto c = Credentials("", "", "");
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, OnlyAccessKeyId) {
  const auto c = Credentials("access_key", "", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AccessKeyIdAndSecretKey) {
  const auto c = Credentials("access_key", "secret_key", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllNonEmpty) {
  const auto c = Credentials("access_key", "secret_key", "session_token");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_EQ("session_token", c.sessionToken());
}

TEST(X509Credentials, CheckRetrieval) {
  const auto c =
      X509Credentials("certb64", X509Credentials::PublicKeySignatureAlgorithm::ECDSA, "serial",
                      "chain", "privatekeypem", SystemTime(std::chrono::seconds(1)));
  EXPECT_EQ("chain", c.certificateChainDerB64());
  EXPECT_EQ("serial", c.certificateSerial());
  EXPECT_EQ("certb64", c.certificateDerB64());
  EXPECT_EQ(SystemTime(std::chrono::seconds(1)), c.certificateExpiration());
  EXPECT_EQ("privatekeypem", c.certificatePrivateKey());
}
class AsyncCredentialHandlingTest : public testing::Test {
public:
  AsyncCredentialHandlingTest()
      : raw_metadata_fetcher_(new MockMetadataFetcher), message_(new Http::RequestMessageImpl()) {};

  void addMethod(const std::string& method) { message_->headers().setMethod(method); }

  void addPath(const std::string& path) { message_->headers().setPath(path); }

  MockMetadataFetcher* raw_metadata_fetcher_;
  MetadataFetcherPtr metadata_fetcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  WebIdentityCredentialsProviderPtr provider_;
  Event::MockTimer* timer_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  Http::RequestMessagePtr message_;
};

TEST_F(AsyncCredentialHandlingTest, ReceivePendingTrueWhenPending) {
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};

  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();

  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri_2"));

  provider_ = std::make_shared<WebIdentityCredentialsProvider>(
      context_, mock_manager_, "cluster_2",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);
  auto chain = std::make_shared<CredentialsProviderChain>();
  chain->add(provider_);
  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      "vpc-lattice-svcs", "ap-southeast-2", chain, context_,
      Common::Aws::AwsSigningHeaderExclusionVector{});

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _)).WillRepeatedly(Invoke([&signer]() {
    // This will check that we see true from credentialsPending by the time we call fetch
    auto cb = Envoy::Extensions::Common::Aws::CredentialsPendingCallback{};
    Http::RequestMessagePtr message(new Http::RequestMessageImpl());

    auto result = signer->addCallbackIfCredentialsPending(std::move(cb));
    EXPECT_TRUE(result);
  }));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
}

TEST_F(AsyncCredentialHandlingTest, ChainCallbackCalledWhenCredentialsReturned) {
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};
  // ::testing::FLAGS_gmock_verbose = "error";
  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();

  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri_2"));

  provider_ = std::make_shared<WebIdentityCredentialsProvider>(
      context_, mock_manager_, "cluster_2",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate());
  EXPECT_CALL(*chain, chainGetCredentials()).WillRepeatedly(Return(Credentials("akid", "skid")));

  auto document = R"EOF(
  {
    "AssumeRoleWithWebIdentityResponse": {
      "AssumeRoleWithWebIdentityResult": {
        "Credentials": {
          "AccessKeyId": "akid",
          "SecretAccessKey": "secret",
          "SessionToken": "token",
          "Expiration": 1.514869445E9
        }
      }
    }
  }
  )EOF";

  auto handle = provider_->subscribeToCredentialUpdates(chain);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      "vpc-lattice-svcs", "ap-southeast-2", chain, context_,
      Common::Aws::AwsSigningHeaderExclusionVector{});
  addMethod("GET");
  addPath("/");

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _))
      .WillRepeatedly(
          Invoke([&, document = std::move(document)](Http::RequestMessage&, Tracing::Span&,
                                                     MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(document));
          }));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
  // We now have credentials so sign should complete immediately
  auto result = signer->sign(*message_, false, "");
  ASSERT_TRUE(result.ok());
}

TEST_F(AsyncCredentialHandlingTest, SubscriptionsCleanedUp) {
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};

  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri_2"));

  provider_ = std::make_shared<WebIdentityCredentialsProvider>(
      context_, mock_manager_, "cluster_2",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  timer_->enableTimer(std::chrono::milliseconds(1), nullptr);

  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate());
  EXPECT_CALL(*chain, chainGetCredentials()).WillRepeatedly(Return(Credentials("akid", "skid")));
  auto chain2 = std::make_shared<MockCredentialsProviderChain>();

  auto document = R"EOF(
  {
    "AssumeRoleWithWebIdentityResponse": {
      "AssumeRoleWithWebIdentityResult": {
        "Credentials": {
          "AccessKeyId": "akid",
          "SecretAccessKey": "secret",
          "SessionToken": "token",
          "Expiration": 1.514869445E9
        }
      }
    }
  }
  )EOF";

  auto handle = provider_->subscribeToCredentialUpdates(chain);
  auto handle2 = provider_->subscribeToCredentialUpdates(chain);

  auto signer = std::make_unique<Extensions::Common::Aws::SigV4SignerImpl>(
      "vpc-lattice-svcs", "ap-southeast-2", chain, context_,
      Common::Aws::AwsSigningHeaderExclusionVector{});
  addMethod("GET");
  addPath("/");

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _))
      .WillRepeatedly(
          Invoke([&, document = std::move(document)](Http::RequestMessage&, Tracing::Span&,
                                                     MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(document));
          }));

  handle2.reset();
  chain2.reset();

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
  // We now have credentials so sign should complete immediately
  auto result = signer->sign(*message_, false, "");
  ASSERT_TRUE(result.ok());
}

// Mock WebIdentityCredentialsProvider to track refresh calls
class MockWebIdentityProvider : public WebIdentityCredentialsProvider {
public:
  MockWebIdentityProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider& config)
      : WebIdentityCredentialsProvider(context, aws_cluster_manager, cluster_name,
                                       create_metadata_fetcher_cb, refresh_state,
                                       initialization_timer, config) {}
  MOCK_METHOD(void, refresh, (), (override));
};

TEST_F(AsyncCredentialHandlingTest, WeakPtrProtectionInTimerCallback) {

  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};
  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("token");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("session");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri"));

  auto mock_provider = std::make_shared<MockWebIdentityProvider>(
      context_, mock_manager_, "cluster",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  Event::MockTimer* timer_ptr = timer_; // Keep raw pointer to test after provider destruction
  auto provider_friend = MetadataCredentialsProviderBaseFriend(mock_provider);

  // When provider is alive, refresh should be called
  EXPECT_CALL(*mock_provider, refresh());
  provider_friend.onClusterAddOrUpdate();
  timer_ptr->enabled_ = true;
  timer_ptr->invokeCallback();
  delete (raw_metadata_fetcher_);
}

TEST_F(AsyncCredentialHandlingTest, WeakPtrProtectionForStatsInTimerCallback) {
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};
  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("token");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("session");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri"));

  auto mock_provider = std::make_shared<MockWebIdentityProvider>(
      context_, mock_manager_, "cluster",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);

  timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  Event::MockTimer* timer_ptr = timer_;
  auto provider_friend = MetadataCredentialsProviderBaseFriend(mock_provider);
  provider_friend.onClusterAddOrUpdate();

  // Invalidate stats pointer
  provider_friend.invalidateStats();

  // Timer callback will skip the stats call due to weak_ptr lock failing
  EXPECT_CALL(*mock_provider, refresh());
  timer_ptr->enabled_ = true;
  timer_ptr->invokeCallback();
  delete (raw_metadata_fetcher_);
}

TEST_F(AsyncCredentialHandlingTest, WeakPtrProtectionInSubscriberCallback) {
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state =
      MetadataFetcher::MetadataReceiver::RefreshState::Ready;
  std::chrono::seconds initialization_timer = std::chrono::seconds(2);

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};
  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("token");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("session");

  mock_manager_ = std::make_shared<MockAwsClusterManager>();
  EXPECT_CALL(*mock_manager_, getUriFromClusterName(_)).WillRepeatedly(Return("uri"));

  provider_ = std::make_shared<WebIdentityCredentialsProvider>(
      context_, mock_manager_, "cluster",
      [this](Upstream::ClusterManager&, absl::string_view) {
        metadata_fetcher_.reset(raw_metadata_fetcher_);
        return std::move(metadata_fetcher_);
      },
      refresh_state, initialization_timer, cred_provider);

  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider_);

  // Test 1: When subscriber is alive, onCredentialUpdate should be called
  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate());
  auto handle = provider_->subscribeToCredentialUpdates(chain);

  // Trigger credential update
  provider_friend.setCredentialsToAllThreads(std::make_unique<Credentials>("key", "secret"));

  // Test 2: When subscriber is destroyed, onCredentialUpdate should not be called
  EXPECT_CALL(*chain, onCredentialUpdate()).Times(0);
  chain.reset(); // Destroy the subscriber

  // Trigger credential update - should not crash due to weak_ptr protection
  provider_friend.setCredentialsToAllThreads(std::make_unique<Credentials>("key2", "secret2"));
  delete (raw_metadata_fetcher_);
}

class ControlledCredentialsProvider : public CredentialsProvider {
public:
  ControlledCredentialsProvider(CredentialSubscriberCallbacks* cb) : cb_(cb) {}

  Credentials getCredentials() override {
    Thread::LockGuard guard(mu_);
    return credentials_;
  }

  bool credentialsPending() override {
    Thread::LockGuard guard(mu_);
    return pending_;
  }

  std::string providerName() override { return "Controlled Credentials Provider"; }

  void refresh(const Credentials& credentials) {
    {
      Thread::LockGuard guard(mu_);
      credentials_ = credentials;
      pending_ = false;
    }
    if (cb_) {
      cb_->onCredentialUpdate();
    }
  }

private:
  CredentialSubscriberCallbacks* cb_;
  Thread::MutexBasicLockable mu_;
  Credentials credentials_ ABSL_GUARDED_BY(mu_);
  bool pending_ ABSL_GUARDED_BY(mu_) = true;
};

TEST(CredentialsProviderChainTest, SignerCallbacksCalledWhenCredentialsReturned) {
  MockFunction<void()> signer_callback;
  EXPECT_CALL(signer_callback, Call());

  CredentialsProviderChain chain;
  auto provider = std::make_shared<ControlledCredentialsProvider>(&chain);
  chain.add(provider);
  ASSERT_TRUE(chain.addCallbackIfChainCredentialsPending(signer_callback.AsStdFunction()));
  provider->refresh(Credentials());
}

TEST(CredentialsProviderChainTest, getCredentials_noCredentials) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  EXPECT_CALL(*mock_provider1, getCredentials());
  EXPECT_CALL(*mock_provider2, getCredentials());

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);
  const absl::StatusOr<Credentials> creds = chain.chainGetCredentials();
  EXPECT_EQ(Credentials(), creds.value());
}

TEST(CredentialsProviderChainTest, getCredentials_firstProviderReturns) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  const Credentials creds("access_key", "secret_key");

  EXPECT_CALL(*mock_provider1, getCredentials()).WillOnce(Return(creds));
  EXPECT_CALL(*mock_provider2, getCredentials()).Times(0);

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  const absl::StatusOr<Credentials> ret_creds = chain.chainGetCredentials();

  EXPECT_EQ(creds, ret_creds.value());
}

TEST(CredentialsProviderChainTest, getCredentials_secondProviderReturns) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  const Credentials creds("access_key", "secret_key");

  EXPECT_CALL(*mock_provider1, getCredentials());
  EXPECT_CALL(*mock_provider2, getCredentials()).WillOnce(Return(creds));

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  const absl::StatusOr<Credentials> ret_creds = chain.chainGetCredentials();
  EXPECT_EQ(creds, ret_creds.value());
}

TEST(CredentialsProviderChainTest, CheckChainReturnsPendingInCorrectOrder) {
  auto mock_provider1 = std::make_shared<MockCredentialsProvider>();
  auto mock_provider2 = std::make_shared<MockCredentialsProvider>();

  EXPECT_CALL(*mock_provider1, getCredentials())
      .WillRepeatedly(Return(Credentials("provider1", "1")));
  EXPECT_CALL(*mock_provider2, getCredentials())
      .WillRepeatedly(Return(Credentials("provider2", "2")));
  EXPECT_CALL(*mock_provider1, providerName()).WillRepeatedly(Return("provider1"));
  EXPECT_CALL(*mock_provider2, providerName()).WillRepeatedly(Return("provider2"));

  CredentialsProviderChain chain;
  chain.add(mock_provider1);
  chain.add(mock_provider2);

  auto cb = Envoy::Extensions::Common::Aws::CredentialsPendingCallback{};
  // We want to ensure that if mock_provider1 returns credentialsPending false, then the credentials
  // from provider1 are used Mock provider 2 credentialsPending will never be called as provider 1
  // will trigger early exit
  EXPECT_CALL(*mock_provider1, credentialsPending()).WillOnce(Return(false));
  EXPECT_CALL(*mock_provider2, credentialsPending()).Times(0);

  bool pending = chain.addCallbackIfChainCredentialsPending(std::move(cb));
  EXPECT_EQ(pending, false);
  auto creds = chain.chainGetCredentials();
  EXPECT_EQ(creds.accessKeyId(), "provider1");
  EXPECT_EQ(creds.secretAccessKey(), "1");
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy

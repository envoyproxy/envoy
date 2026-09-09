#include "source/common/http/message_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using Envoy::Extensions::Common::Aws::MetadataFetcherPtr;
using testing::MockFunction;
using testing::Return;
using testing::ReturnRef;

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
      Common::Aws::AwsSigningHeaderMatcherVector{}, Common::Aws::AwsSigningHeaderMatcherVector{});

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

  // Subscribers are notified once from the thread that initiates the update and again once
  // every thread has applied it. ThreadLocal::MockInstance runs both synchronously.
  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate()).Times(2);
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
      Common::Aws::AwsSigningHeaderMatcherVector{}, Common::Aws::AwsSigningHeaderMatcherVector{});
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
  ASSERT_OK(result);
}

TEST_F(AsyncCredentialHandlingTest, ExpirationWithGracePeriod) {
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

  // Set expiration time 10 minutes from now
  auto future_time = context_.api().timeSource().systemTime() + std::chrono::minutes(10);
  auto expiration_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(future_time.time_since_epoch()).count();

  auto document = fmt::format(R"EOF(
  {{
    "AssumeRoleWithWebIdentityResponse": {{
      "AssumeRoleWithWebIdentityResult": {{
        "Credentials": {{
          "AccessKeyId": "akid",
          "SecretAccessKey": "secret",
          "SessionToken": "token",
          "Expiration": {}
        }}
      }}
    }}
  }}
  )EOF",
                              expiration_timestamp);

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessage&, Tracing::Span&, MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(document));
          }));

  // Expect timer to be set to less than 10 minutes due to grace period
  EXPECT_CALL(*timer_, enableTimer(testing::Lt(std::chrono::minutes(10)), nullptr))
      .Times(testing::AtLeast(1));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
}

TEST_F(AsyncCredentialHandlingTest, ExpirationTooCloseToGracePeriod) {
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

  // Set expiration time only 10 seconds from now (less than grace period)
  auto future_time = context_.api().timeSource().systemTime() + std::chrono::seconds(10);
  auto expiration_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(future_time.time_since_epoch()).count();

  auto document = fmt::format(R"EOF(
  {{
    "AssumeRoleWithWebIdentityResponse": {{
      "AssumeRoleWithWebIdentityResult": {{
        "Credentials": {{
          "AccessKeyId": "akid",
          "SecretAccessKey": "secret",
          "SessionToken": "token",
          "Expiration": {}
        }}
      }}
    }}
  }}
  )EOF",
                              expiration_timestamp);

  EXPECT_CALL(*raw_metadata_fetcher_, fetch(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessage&, Tracing::Span&, MetadataFetcher::MetadataReceiver& receiver) {
            receiver.onMetadataSuccess(std::move(document));
          }));

  // Expect timer to be set multiple times - first 1ms for initial trigger, then 1000ms for
  // immediate refresh
  EXPECT_CALL(*timer_, enableTimer(_, nullptr)).Times(testing::AtLeast(1));

  provider_friend.onClusterAddOrUpdate();
  timer_->invokeCallback();
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

  // Subscribers are notified once from the thread that initiates the update and again once
  // every thread has applied it. ThreadLocal::MockInstance runs both synchronously.
  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate()).Times(2);
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
      Common::Aws::AwsSigningHeaderMatcherVector{}, Common::Aws::AwsSigningHeaderMatcherVector{});
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
  ASSERT_OK(result);
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

  // Test 1: When subscriber is alive, onCredentialUpdate should be called. It is called once from
  // the updating thread and again once all threads have applied the update.
  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate()).Times(2);
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

// Regression tests for a deadlock between credential resolution and worker thread startup.
//
// credentials_pending_ used to be cleared from the all-threads-complete callback of
// ThreadLocal::Slot::runOnAllThreads(). That callback only fires once every registered worker
// dispatcher has run the update, and worker dispatchers are registered when the ListenerManager is
// constructed but do not start running until startWorkers(), which itself waits on the server init
// manager. Any credential refresh that resolved before that point latched credentials_pending_ at
// true forever and stalled every signer depending on it.
//
// These tests use a real ThreadLocal::InstanceImpl with a registered worker dispatcher that is
// deliberately never run(), which is exactly the pre-startWorkers() state. Every other AWS test
// uses ThreadLocal::MockInstance, whose runOnAllThreads() invokes both callbacks synchronously, so
// the barrier is never real there.
class CredentialsPendingBeforeWorkerStartupTest : public testing::Test {
public:
  CredentialsPendingBeforeWorkerStartupTest()
      : api_(Api::createApiForTest()), main_dispatcher_(api_->allocateDispatcher("test_main")),
        worker_dispatcher_(api_->allocateDispatcher("test_worker")),
        mock_manager_(std::make_shared<MockAwsClusterManager>()) {
    tls_.registerThread(*main_dispatcher_, /*main_thread=*/true);
    // Registered but never run(), modelling a worker thread that has not been started yet.
    tls_.registerThread(*worker_dispatcher_, /*main_thread=*/false);

    // Must be in place before the provider is constructed: MetadataCredentialsProviderBase's
    // constructor allocates its tls slot from context_.threadLocal().
    ON_CALL(context_, threadLocal()).WillByDefault(ReturnRef(tls_));
    ON_CALL(*mock_manager_, getUriFromClusterName(_)).WillByDefault(Return("uri_2"));
  }

  ~CredentialsPendingBeforeWorkerStartupTest() override {
    // The provider owns a slot in tls_, so it has to go away while tls_ is still alive and not yet
    // shut down.
    provider_.reset();
    tls_.shutdownGlobalThreading();
    tls_.shutdownThread();
  }

  void createProvider() {
    envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
        {};
    cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
    cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
    cred_provider.set_role_session_name("role-session-name");

    // These tests drive setCredentialsToAllThreads() directly rather than through a fetch, so the
    // metadata fetcher is never created.
    provider_ = std::make_shared<WebIdentityCredentialsProvider>(
        context_, mock_manager_, "cluster_2",
        [](Upstream::ClusterManager&, absl::string_view) { return MetadataFetcherPtr{}; },
        MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
        cred_provider);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr main_dispatcher_;
  Event::DispatcherPtr worker_dispatcher_;
  ThreadLocal::InstanceImpl tls_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  WebIdentityCredentialsProviderPtr provider_;
};

// A successful credential refresh must un-pend the provider and notify its subscribers even though
// no worker dispatcher is running.
TEST_F(CredentialsPendingBeforeWorkerStartupTest, UnpendsBeforeWorkerDispatchersRun) {
  createProvider();
  EXPECT_TRUE(provider_->credentialsPending());

  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate());
  auto handle = provider_->subscribeToCredentialUpdates(chain);

  MetadataCredentialsProviderBaseFriend(provider_).setCredentialsToAllThreads(
      std::make_unique<Credentials>("akid", "secret", "token"));

  EXPECT_FALSE(provider_->credentialsPending());
  // The main thread's slot is written synchronously by runOnAllThreads(), so the credentials are
  // readable here as well.
  const auto credentials = provider_->getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId());
  EXPECT_EQ("secret", credentials.secretAccessKey());
  EXPECT_EQ("token", credentials.sessionToken());
}

// The failure path must un-pend too.
TEST_F(CredentialsPendingBeforeWorkerStartupTest, UnpendsOnRetrievalErrorBeforeWorkersRun) {
  createProvider();
  EXPECT_TRUE(provider_->credentialsPending());

  auto chain = std::make_shared<MockCredentialsProviderChain>();
  EXPECT_CALL(*chain, onCredentialUpdate());
  auto handle = provider_->subscribeToCredentialUpdates(chain);

  MetadataCredentialsProviderBaseFriend(provider_).credentialsRetrievalError();

  EXPECT_FALSE(provider_->credentialsPending());
  EXPECT_FALSE(provider_->getCredentials().accessKeyId().has_value());
}

// A thread must never observe "credentials are no longer pending" before the credential update
// itself has been applied to that thread. The pending flag and the credentials both live in the
// thread local cache and move as one update, so the pair is always consistent per thread.
//
// This test parks a running worker dispatcher inside one of its own callbacks, performs a
// credential update from the main thread while the worker is stuck there, and then lets the worker
// observe both halves.
class CredentialsPendingWorkerVisibilityTest : public testing::Test {
public:
  CredentialsPendingWorkerVisibilityTest()
      : api_(Api::createApiForTest()), main_dispatcher_(api_->allocateDispatcher("test_main")),
        worker_dispatcher_(api_->allocateDispatcher("test_worker")),
        mock_manager_(std::make_shared<MockAwsClusterManager>()) {
    tls_.registerThread(*main_dispatcher_, /*main_thread=*/true);
    tls_.registerThread(*worker_dispatcher_, /*main_thread=*/false);

    ON_CALL(context_, threadLocal()).WillByDefault(ReturnRef(tls_));
    ON_CALL(*mock_manager_, getUriFromClusterName(_)).WillByDefault(Return("uri_2"));
  }

  ~CredentialsPendingWorkerVisibilityTest() override {
    if (worker_thread_ != nullptr) {
      worker_dispatcher_->exit();
      worker_thread_->join();
    }
    provider_.reset();
    tls_.shutdownGlobalThreading();
    tls_.shutdownThread();
  }

  void createProvider() {
    envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
        {};
    cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
    cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
    cred_provider.set_role_session_name("role-session-name");

    provider_ = std::make_shared<WebIdentityCredentialsProvider>(
        context_, mock_manager_, "cluster_2",
        [](Upstream::ClusterManager&, absl::string_view) { return MetadataFetcherPtr{}; },
        MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
        cred_provider);
  }

  // Started after the provider exists, so the slot initializer that the provider's constructor
  // posted is already queued ahead of anything this test posts. RunUntilExit is what production
  // worker threads use: RunType::Block returns as soon as the event set drains, which for a
  // dispatcher with no listeners on it means the thread would exit between posts.
  void startWorker() {
    worker_thread_ = api_->threadFactory().createThread(
        [this]() { worker_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });
  }

  // Runs cb on the worker thread and waits for it to finish.
  void runOnWorker(std::function<void()> cb) {
    absl::Notification done;
    worker_dispatcher_->post([&]() {
      cb();
      done.Notify();
    });
    done.WaitForNotification();
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr main_dispatcher_;
  Event::DispatcherPtr worker_dispatcher_;
  Thread::ThreadPtr worker_thread_;
  ThreadLocal::InstanceImpl tls_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::shared_ptr<MockAwsClusterManager> mock_manager_;
  WebIdentityCredentialsProviderPtr provider_;
};

TEST_F(CredentialsPendingWorkerVisibilityTest, WorkerNeverSeesUnpendedStaleCredentials) {
  createProvider();
  startWorker();

  absl::Notification worker_parked;
  absl::Notification release_worker;
  absl::Notification observation_done;
  bool observed_pending = false;
  bool observed_has_credentials = false;

  // Occupies the worker's event loop, so the credential update posted below is queued behind this
  // callback and cannot be applied until it returns.
  worker_dispatcher_->post([&]() {
    worker_parked.Notify();
    release_worker.WaitForNotification();
    observed_pending = provider_->credentialsPending();
    observed_has_credentials = provider_->getCredentials().hasCredentials();
    observation_done.Notify();
  });
  worker_parked.WaitForNotification();

  MetadataCredentialsProviderBaseFriend(provider_).setCredentialsToAllThreads(
      std::make_unique<Credentials>("akid", "secret", "token"));
  // Visible on the main thread immediately, which is what lets a callout made before startWorkers()
  // be signed.
  EXPECT_FALSE(provider_->credentialsPending());
  EXPECT_TRUE(provider_->getCredentials().hasCredentials());

  release_worker.Notify();
  observation_done.WaitForNotification();

  // The worker had not applied the update yet, so it has to still report pending.
  EXPECT_TRUE(observed_pending);
  EXPECT_FALSE(observed_has_credentials);

  // Once it drains its post queue, both halves are visible together.
  bool drained_pending = true;
  bool drained_has_credentials = false;
  runOnWorker([&]() {
    drained_pending = provider_->credentialsPending();
    drained_has_credentials = provider_->getCredentials().hasCredentials();
  });
  EXPECT_FALSE(drained_pending);
  EXPECT_TRUE(drained_has_credentials);
}

// A pending callback registered by a worker after the updating thread has already notified
// subscribers must still be woken. The worker keeps reading credentials_pending_ == true from its
// own slot until it applies the posted update, so it can queue a callback onto a queue that has
// just been drained. The all-threads-complete notification is what rescues it; without it the
// request stalls until the next successful refresh, ~1 hour later.
TEST_F(CredentialsPendingWorkerVisibilityTest, PendingCallbackRegisteredDuringStaleWindowIsWoken) {
  createProvider();
  startWorker();

  auto chain = std::make_shared<CredentialsProviderChain>();
  chain->add(provider_);
  auto handle = provider_->subscribeToCredentialUpdates(chain);

  absl::Notification worker_parked;
  absl::Notification release_worker;
  absl::Notification registered;
  bool was_pending = false;
  std::atomic<bool> callback_fired{false};

  // Parks the worker's event loop so that the credential update posted below cannot be applied to
  // the worker's slot until we let it go.
  worker_dispatcher_->post([&]() {
    worker_parked.Notify();
    release_worker.WaitForNotification();
    // The worker's slot still says pending, so the chain queues this callback - after the main
    // thread has already drained the queue.
    was_pending =
        chain->addCallbackIfChainCredentialsPending([&callback_fired]() { callback_fired = true; });
    registered.Notify();
  });
  worker_parked.WaitForNotification();

  MetadataCredentialsProviderBaseFriend(provider_).setCredentialsToAllThreads(
      std::make_unique<Credentials>("akid", "secret", "token"));

  release_worker.Notify();
  registered.WaitForNotification();
  EXPECT_TRUE(was_pending);
  EXPECT_FALSE(callback_fired);

  // Let the worker drain the credential update. Releasing the last reference to the update callback
  // posts the all-threads-complete callback onto the main dispatcher.
  runOnWorker([]() {});
  main_dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_TRUE(callback_fired);
}

// Marking pending again while already pending must not broadcast.
// InstanceProfileCredentialsProvider marks pending once per stage of its three stage fetch, and
// only the first transition needs an update posted to every thread.
TEST(MetadataCredentialsProviderPendingTest, MarkingPendingIsDeduplicated) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto mock_manager = std::make_shared<MockAwsClusterManager>();
  ON_CALL(*mock_manager, getUriFromClusterName(_)).WillByDefault(Return("uri_2"));

  envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider cred_provider =
      {};
  cred_provider.mutable_web_identity_token_data_source()->set_inline_string("abced");
  cred_provider.set_role_arn("aws:iam::123456789012:role/arn");
  cred_provider.set_role_session_name("role-session-name");

  auto provider = std::make_shared<WebIdentityCredentialsProvider>(
      context, mock_manager, "cluster_2",
      [](Upstream::ClusterManager&, absl::string_view) { return MetadataFetcherPtr{}; },
      MetadataFetcher::MetadataReceiver::RefreshState::Ready, std::chrono::seconds(2),
      cred_provider);
  auto provider_friend = MetadataCredentialsProviderBaseFriend(provider);

  // A provider starts out pending, so there is nothing to broadcast.
  EXPECT_CALL(context.thread_local_, runOnAllThreads(_)).Times(0);
  provider_friend.setCredentialsPendingToAllThreads();
  EXPECT_TRUE(provider->credentialsPending());
  testing::Mock::VerifyAndClearExpectations(&context.thread_local_);

  // Delivering the credentials and clearing the flag uses the two-argument overload, so that
  // subscribers are notified again once every thread has applied the update. Setting the flag again
  // is then the only single-argument broadcast: the second, redundant call posts nothing.
  EXPECT_CALL(context.thread_local_, runOnAllThreads(_, _));
  EXPECT_CALL(context.thread_local_, runOnAllThreads(_));
  provider_friend.setCredentialsToAllThreads(std::make_unique<Credentials>("akid", "secret"));
  EXPECT_FALSE(provider->credentialsPending());
  provider_friend.setCredentialsPendingToAllThreads();
  provider_friend.setCredentialsPendingToAllThreads();
  EXPECT_TRUE(provider->credentialsPending());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy

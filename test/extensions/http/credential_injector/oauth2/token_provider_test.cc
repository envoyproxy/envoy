#include "source/common/http/headers.h"
#include "source/extensions/http/injected_credentials/common/secret_reader.h"
#include "source/extensions/http/injected_credentials/oauth2/client_credentials_impl.h"
#include "source/extensions/http/injected_credentials/oauth2/token_provider.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

using testing::NiceMock;

class MockSecretReader : public Http::InjectedCredentials::Common::SecretReader {
public:
  MockSecretReader(const std::string& secret) : secret_(secret) {};
  const std::string& credential() const override { return secret_; }

private:
  const std::string secret_;
};

TEST(TokenProvider, TokenProviderTest) {
  const std::string yaml_string = R"EOF(
      token_fetch_retry_interval: 5s
      token_endpoint:
        cluster: non-existing-cluster
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: "client-id"
        client_secret: {}
  )EOF";

  envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2 proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Event::MockDispatcher> dispatcher;
  auto secret_reader = std::make_shared<MockSecretReader>("");
  auto token_provider =
      std::make_shared<TokenProvider>(secret_reader, tls, cluster_manager, proto_config, dispatcher,
                                      "stats_prefix", context.serverFactoryContext().scope());
  EXPECT_NO_THROW(token_provider->asyncGetAccessToken());
  EXPECT_NO_THROW(token_provider->onGetAccessTokenSuccess("token", std::chrono::seconds(10)));
  EXPECT_NO_THROW(
      token_provider->onGetAccessTokenFailure(FilterCallbacks::FailureReason::StreamReset));
}

// Simulate a successful token fetch followed by a fetch failure while the token has NOT yet expired. 
TEST(TokenProvider, FetchFailureInjectsStaleTokenWhenNotExpired) {
  const std::string yaml_string = R"EOF(
      token_fetch_retry_interval: 5s
      token_endpoint:
        cluster: non-existing-cluster
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: "client-id"
        client_secret: {}
  )EOF";

  envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2 proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Event::MockDispatcher> dispatcher;
  auto secret_reader = std::make_shared<MockSecretReader>("client-secret");

  auto token_provider =
      std::make_shared<TokenProvider>(secret_reader, tls, cluster_manager, proto_config, dispatcher,
                                      "stats_prefix", context.serverFactoryContext().scope());

  token_provider->onGetAccessTokenSuccess("valid-access-token", std::chrono::seconds(3600));
  EXPECT_EQ("Bearer valid-access-token", token_provider->credential());

  token_provider->onGetAccessTokenFailure(FilterCallbacks::FailureReason::StreamReset);
  EXPECT_EQ("Bearer valid-access-token", token_provider->credential());

  // inject() succeeds because the token is non-empty and not expired.
  auto injector = std::make_shared<OAuth2ClientCredentialTokenInjector>(token_provider);
  Envoy::Http::TestRequestHeaderMapImpl headers;
  absl::Status status = injector->inject(headers, false);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("Bearer valid-access-token",
            headers.get(Envoy::Http::CustomHeaders::get().Authorization)[0]->value().getStringView());
}

// Simulate a successful token fetch followed by a fetch failure after the token has already expired.
TEST(TokenProvider, FetchFailureClearsExpiredTokenAndInjectFails) {
  const std::string yaml_string = R"EOF(
      token_fetch_retry_interval: 5s
      token_endpoint:
        cluster: non-existing-cluster
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: "client-id"
        client_secret: {}
  )EOF";

  envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2 proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Event::MockDispatcher> dispatcher;
  auto secret_reader = std::make_shared<MockSecretReader>("client-secret");

  auto token_provider =
      std::make_shared<TokenProvider>(secret_reader, tls, cluster_manager, proto_config, dispatcher,
                                      "stats_prefix", context.serverFactoryContext().scope());

  // A token is fetched with 0s expiry, making it immediately expired.
  token_provider->onGetAccessTokenSuccess("expired-access-token", std::chrono::seconds(0));
  EXPECT_EQ("Bearer expired-access-token", token_provider->credential());

  // The expired token has been cleared.
  token_provider->onGetAccessTokenFailure(FilterCallbacks::FailureReason::StreamReset);
  EXPECT_TRUE(token_provider->credential().empty());

  auto injector = std::make_shared<OAuth2ClientCredentialTokenInjector>(token_provider);
  Envoy::Http::TestRequestHeaderMapImpl headers;
  absl::Status status = injector->inject(headers, false);
  EXPECT_TRUE(absl::IsNotFound(status));
  EXPECT_TRUE(headers.get(Envoy::Http::CustomHeaders::get().Authorization).empty());
}

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy

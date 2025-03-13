#include "source/extensions/http/injected_credentials/common/secret_reader.h"
#include "source/extensions/http/injected_credentials/oauth2/token_provider.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

using testing::NiceMock;

class MockSecretReader : public Http::InjectedCredentials::Common::SecretReader {
public:
  MockSecretReader(const std::string& secret) : secret_(secret){};
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

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy

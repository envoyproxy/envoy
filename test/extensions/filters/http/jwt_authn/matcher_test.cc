#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/requirement_matchers.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

using ::envoy::api::v2::route::RouteMatch;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement;
using ::Envoy::Http::TestHeaderMapImpl;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& response_body)
      : request_(&mock_cm.async_client_), response_body_(response_body) {
    ON_CALL(mock_cm.async_client_, send_(_, _, _))
        .WillByDefault(Invoke([this](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
                                     const absl::optional<std::chrono::milliseconds>&)
                                  -> Http::AsyncClient::Request* {
          Http::MessagePtr response_message(new Http::ResponseMessageImpl(
              Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
          response_message->body().reset(new Buffer::OwnedImpl(response_body_));
          cb.onSuccess(std::move(response_message));
          called_count_++;
          return &request_;
        }));
  }

  int called_count() const { return called_count_; }

private:
  Http::MockAsyncClientRequest request_;
  std::string response_body_;
  int called_count_{};
};

class MockAsyncMatcherCallbacks : public AsyncMatcher::Callbacks {
public:
  MOCK_METHOD1(onComplete, void(const ::google::jwt_verify::Status& status));
};

class MatcherTest : public ::testing::Test {
public:
  void SetUp() { MessageUtil::loadFromYaml(ExampleConfig, proto_config_); }

  void createAsyncMatcher() {
    FilterConfigSharedPtr filter_config =
        std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);

    JwtRequirement requirement;
    MessageUtil::loadFromYaml(std::string("provider_name: ") + ProviderName, requirement);
    matcher_ = AsyncMatcher::create(requirement, filter_config);
  }

  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  JwtAuthentication proto_config_;
  AsyncMatcherSharedPtr matcher_;
  MockAsyncMatcherCallbacks mock_cb_;
};

TEST_F(MatcherTest, TestMatchPrefix) {
  const char config[] = R"(prefix: "/match")";
  RouteMatch route;
  MessageUtil::loadFromYaml(config, route);
  MatcherConstSharedPtr matcher = Matcher::create(route);
  auto headers = TestHeaderMapImpl{{":path", "/match/this"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matching"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/no"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchRegex) {
  const char config[] = R"(regex: "/[^c][au]t")";
  RouteMatch route;
  MessageUtil::loadFromYaml(config, route);
  MatcherConstSharedPtr matcher = Matcher::create(route);
  auto headers = TestHeaderMapImpl{{":path", "/but"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/mat?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/maut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/cut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/mut/"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPath) {
  const char config[] = R"(path: "/match"
case_sensitive: false)";
  RouteMatch route;
  MessageUtil::loadFromYaml(config, route);
  MatcherConstSharedPtr matcher = Matcher::create(route);
  auto headers = TestHeaderMapImpl{{":path", "/match"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/match?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/match/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matching"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchQuery) {
  const char config[] = R"(prefix: "/"
query_parameters:
- name: foo
  value: bar)";
  RouteMatch route;
  MessageUtil::loadFromYaml(config, route);
  MatcherConstSharedPtr matcher = Matcher::create(route);
  auto headers = TestHeaderMapImpl{{":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/foo?bar=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?foo"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?bar=foo"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchHeader) {
  const char config[] = R"(prefix: "/"
headers:
- name: a)";
  RouteMatch route;
  MessageUtil::loadFromYaml(config, route);
  MatcherConstSharedPtr matcher = Matcher::create(route);
  auto headers = TestHeaderMapImpl{{":path", "/"}, {"a", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"a", "some"}, {"b", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"aa", ""}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"", ""}};
  EXPECT_FALSE(matcher->matches(headers));
}

// Deeply nested anys that ends in provider name
TEST_F(MatcherTest, DeeplyNestedAnys) {
  const char config[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    forward_payload_header: sec-istio-auth-userinfo
    from_params:
    - jwta
    - jwtb
    - jwtc
rules:
- match: { path: "/match" }
  requires:
    requires_any:
      requirements:
      - requires_any:
          requirements:
          - requires_any:
              requirements:
              - provider_name: "example_provider"
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  matcher_ = AsyncMatcher::create(proto_config_.rules()[0], filter_config);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path", "/match?jwta=" + std::string(GoodToken) + "&jwtb=" + std::string(ExpiredToken)},
  };
  matcher_->matches(headers, mock_cb_);
}

// require alls that just ends
TEST_F(MatcherTest, CanHandleUnexpectedEnd) {
  const char config[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    forward_payload_header: sec-istio-auth-userinfo
rules:
- match: { path: "/match" }
  requires:
    requires_all:
      requirements:
      - requires_all:
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  matcher_ = AsyncMatcher::create(proto_config_.rules()[0], filter_config);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{{":path", "/match"}};
  matcher_->matches(headers, mock_cb_);
}

// This test verifies if no requirements match the path, JWT is not verified.
TEST_F(MatcherTest, TestPathDoesNotMatchRequirements) {
  auto& pathRule1 = *proto_config_.add_rules();
  pathRule1.mutable_match()->set_path("/nomatch");
  pathRule1.mutable_requires()->set_provider_name(ProviderName);
  auto& pathRule2 = *proto_config_.add_rules();
  pathRule2.mutable_match()->set_path("/path");
  pathRule2.mutable_requires()->set_provider_name(ProviderName);
  auto& prefixRule = *proto_config_.add_rules();
  prefixRule.mutable_match()->set_prefix("/path");
  prefixRule.mutable_requires()->set_provider_name(ProviderName);
  auto& regexRule = *proto_config_.add_rules();
  regexRule.mutable_match()->set_regex("/[^N]\\w+");
  regexRule.mutable_requires()->set_provider_name(ProviderName);

  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto headers = Http::TestHeaderMapImpl{{":path", "/NOMATCH"}};

  EXPECT_CALL(mock_cb_, onComplete(_)).Times(5).WillRepeatedly(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  for (const auto& it : proto_config_.rules()) {
    matcher_ = AsyncMatcher::create(it, filter_config);
    matcher_->matches(headers, mock_cb_);
  }
}

// This test verifies if path match but no requirements set, JWT is not verified.
TEST_F(MatcherTest, TestPathRequirementsNoSet) {
  auto& match = *proto_config_.add_rules()->mutable_match();
  match.set_path("/match");
  match.mutable_case_sensitive()->set_value(false);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto headers = Http::TestHeaderMapImpl{{":path", "/MaTch?a=b"}};

  EXPECT_CALL(mock_cb_, onComplete(_)).Times(2).WillRepeatedly(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  for (const auto& it : proto_config_.rules()) {
    matcher_ = AsyncMatcher::create(it, filter_config);
    matcher_->matches(headers, mock_cb_);
  }
}

// This test verifies that JWT must be issued by the provider specified in the requirement.
TEST_F(MatcherTest, TestTokenRequirementProviderMismatch) {
  const char config[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
  other_provider:
    issuer: other_issuer
rules:
- match:
    path: "/"
  requires:
    provider_name: "other_provider"
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };
  for (const auto& it : proto_config_.rules()) {
    matcher_ = AsyncMatcher::create(it, filter_config);
    matcher_->matches(headers, mock_cb_);
  }
}

TEST_F(MatcherTest, TestRequiresAll) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path",
       "/requires-all?jwt_a=" + std::string(GoodToken) + "&jwt_b=" + std::string(OtherGoodToken)},
  };
  matcher_ = AsyncMatcher::create(proto_config_.rules()[0], filter_config);
  matcher_->matches(headers, mock_cb_);
}

TEST_F(MatcherTest, TestRequiresAllWrongLocations) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));
  auto headers =
      Http::TestHeaderMapImpl{{":path", "/requires-all?jwt_a=" + std::string(OtherGoodToken) +
                                            "&jwt_b=" + std::string(GoodToken)}};
  matcher_ = AsyncMatcher::create(proto_config_.rules()[0], filter_config);
  matcher_->matches(headers, mock_cb_);
}

TEST_F(MatcherTest, TestRequiresAny) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);

  EXPECT_CALL(mock_cb_, onComplete(_))
      .Times(3)
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::Ok); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::Ok); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::JwtUnknownIssuer); }));
  auto headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(InvalidAudToken)},
      {":path", "/requires-any"},
  };
  matcher_ = AsyncMatcher::create(proto_config_.rules()[0], filter_config);
  matcher_->matches(headers, mock_cb_);

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(InvalidAudToken)},
      {"b", "Bearer " + std::string(OtherGoodToken)},
      {":path", "/requires-any"},
  };
  matcher_->matches(headers, mock_cb_);

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(InvalidAudToken)},
      {"b", "Bearer " + std::string(InvalidAudToken)},
      {":path", "/requires-any"},
  };
  matcher_->matches(headers, mock_cb_);
}

TEST_F(MatcherTest, TestProviderNameMatcher) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*auth.get(), sanitizePayloadHeaders(_)).Times(1);
  EXPECT_CALL(*auth.get(), verify(_, _, _, _))
      .WillOnce(
          Invoke([](const ExtractParam*, const absl::optional<std::string>&, Http::HeaderMap&,
                    Authenticator::Callbacks* callback) { callback->onComplete(Status::Ok); }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);

  auto matcher =
      std::make_unique<ProviderNameMatcher>(ProviderName, filter_config, std::move(auth));

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };

  matcher->matches(headers, mock_cb_);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

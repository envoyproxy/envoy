#include "extensions/filters/http/jwt_authn/filter_config.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

constexpr char kConfigTemplate[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    from_headers:
    - name: "x-example"
      value_prefix: ""
    forward_payload_header: "x-example-payload"
    local_jwks:
      inline_string: ""
  other_provider:
    issuer: https://other.com
    from_headers:
    - name: "x-other"
      value_prefix: ""
    forward_payload_header: "x-other-payload"
    local_jwks:
      inline_string: ""
rules:
- match:
    path: "/"
)";

constexpr char kExampleHeader[] = "x-example";
constexpr char kOtherHeader[] = "x-other";

// Returns true if the jwt_header is empty, and the jwt_header payload exists.
// Based on the JWT provider setup for this test, this matcher is equivalent to JWT verification
// was success.
MATCHER_P(JwtOutputSuccess, jwt_header, "") {
  auto payload_header = absl::StrCat(jwt_header, "-payload");
  return !arg.has(std::string(jwt_header)) && arg.has(payload_header);
}

// Returns true if the jwt_header exists, and the jwt_header payload is empty.
// Based on the JWT provider setup for this test, this matcher is equivalent to JWT verification
// was failed.
MATCHER_P(JwtOutputFailed, jwt_header, "") {
  auto payload_header = absl::StrCat(jwt_header, "-payload");
  return arg.has(std::string(jwt_header)) && !arg.has(payload_header);
}

// Returns true if both jwt_header and jwt_header payload are empty.
// This is probably an undesirable outcome of JWT validation and should be fixed in the future.
// We will address this as part of https://github.com/envoyproxy/envoy/issues/8909
MATCHER_P(JwtOutputWeird, jwt_header, "") {
  auto payload_header = absl::StrCat(jwt_header, "-payload");
  return !arg.has(std::string(jwt_header)) && !arg.has(payload_header);
}

class AllVerifierTest : public testing::Test {
public:
  void SetUp() override {
    TestUtility::loadFromYaml(kConfigTemplate, proto_config_);
    for (auto& it : *(proto_config_.mutable_providers())) {
      it.second.mutable_local_jwks()->set_inline_string(PublicKey);
    }
  }

  void createVerifier() {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules(0).requires(), proto_config_.providers(),
                                 *filter_config_, filter_config_->getExtractor());
  }

  void modifyRequirement(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, *proto_config_.mutable_rules(0)->mutable_requires());
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  VerifierConstPtr verifier_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ContextSharedPtr context_;
  MockVerifierCallbacks mock_cb_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

// tests rule that is just match no requires.
TEST_F(AllVerifierTest, TestAllAllow) {
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(2);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, "a"}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// tests requires allow missing or failed. The `allow_missing_or_failed` is defined in a single
// requirement by itself.
class AllowFailedInSingleRequirementTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    proto_config_.mutable_rules(0)->mutable_requires()->mutable_allow_missing_or_failed();
    createVerifier();
  }
};

TEST_F(AllowFailedInSingleRequirementTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInSingleRequirementTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailed(kOtherHeader));
}

// The `allow_missing_or_failed` is defined in an OR-list of requirements.
class AllowFailedInOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_any:
  requirements:
  - provider_name: "example_provider"
  - allow_missing_or_failed: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowFailedInOrListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInOrListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
}

TEST_F(AllowFailedInOrListTest, GoodAndBadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailed(kOtherHeader));
}

TEST_F(AllowFailedInOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  // Note: the first (provider) requirement is satisfied, so the allow_missing_or_failed has not
  // kicked in yet.
  EXPECT_THAT(headers, JwtOutputFailed(kOtherHeader));
}

TEST_F(AllowFailedInOrListTest, BadAndGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
  // Note: the first (provider) requirement is failed, so the allow_missing_or_failed run and
  // validate token by x-other (even though not require).
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

// The `allow_missing_or_failed` is defined in an AND-list of requirements.
class AllowFailedInAndListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_all:
  requirements:
  - provider_name: "example_provider"
  - allow_missing_or_failed: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowFailedInAndListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInAndListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
}

TEST_F(AllowFailedInAndListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {kExampleHeader, GoodToken},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // In AND-mode, the allow-missing-or-failed reset the output payload. This could be an undesired
  // behavior.
  EXPECT_THAT(headers, JwtOutputWeird(kExampleHeader));
}

TEST_F(AllowFailedInAndListTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Same as above, output payload is reset.
  EXPECT_THAT(headers, JwtOutputWeird(kExampleHeader));
  // The bad, non-required token won't affect the verification status though.
  EXPECT_THAT(headers, JwtOutputFailed(kOtherHeader));
}

TEST_F(AllowFailedInAndListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Same as above, output payload is reset.
  EXPECT_THAT(headers, JwtOutputWeird(kExampleHeader));
  // The *other* JWT is processed by allow_missing_or_failed, and actually output payload.
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

class AllowFailedInAndOfOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_all:
  requirements:
  - requires_any:
      requirements:
      - provider_name: "example_provider"
      - allow_missing_or_failed: {}
  - requires_any:
      requirements:
      - provider_name: "other_provider"
      - allow_missing_or_failed: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowFailedInAndOfOrListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInAndOfOrListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Output payload is reset due to the second allow_missing_or_failed. Again, this is not
  // a desirable state.
  EXPECT_THAT(headers, JwtOutputWeird(kExampleHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, OtherGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{{kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Same here, payload for other token is reset.
  EXPECT_THAT(headers, JwtOutputWeird(kOtherHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, BadAndGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, ExpiredToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailed(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputWeird(kOtherHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers =
      Http::TestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

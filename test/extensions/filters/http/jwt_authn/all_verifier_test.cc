#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/extensions/filters/http/jwt_authn/filter_config.h"
#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
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
    forward: true
    local_jwks:
      inline_string: ""
  other_provider:
    issuer: https://other.com
    from_headers:
    - name: "x-other"
      value_prefix: ""
    forward_payload_header: "x-other-payload"
    forward: true
    local_jwks:
      inline_string: ""
rules:
- match:
    path: "/"
)";

constexpr char kExampleHeader[] = "x-example";
constexpr char kOtherHeader[] = "x-other";

// Returns true if jwt_header payload exists.
// Payload is added only after verification was success.
MATCHER_P(JwtOutputSuccess, jwt_header, "") {
  auto payload_header = absl::StrCat(jwt_header, "-payload");
  return arg.has(payload_header);
}

// Returns true if the jwt_header payload is empty.
// Payload is added only after verification was success.
MATCHER_P(JwtOutputFailedOrIgnore, jwt_header, "") {
  auto payload_header = absl::StrCat(jwt_header, "-payload");
  return !arg.has(payload_header);
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
    filter_config_ = std::make_shared<FilterConfigImpl>(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules(0).requires_(), proto_config_.providers(),
                                 *filter_config_);
  }

  void modifyRequirement(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, *proto_config_.mutable_rules(0)->mutable_requires_());
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::shared_ptr<FilterConfigImpl> filter_config_;
  VerifierConstPtr verifier_;
  ContextSharedPtr context_;
  MockVerifierCallbacks mock_cb_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

// tests rule that is just match no requires.
TEST_F(AllVerifierTest, TestAllAllow) {
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(2);
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, "a"}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// tests requires allow missing or failed. The `allow_missing_or_failed` is defined in a single
// requirement by itself.
class AllowFailedInSingleRequirementTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    proto_config_.mutable_rules(0)->mutable_requires_()->mutable_allow_missing_or_failed();
    createVerifier();
  }
};

TEST_F(AllowFailedInSingleRequirementTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInSingleRequirementTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, MissingIssToken) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ES256WithoutIssToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(AllowFailedInSingleRequirementTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

// The `allow_missing_or_failed` is defined in an OR-list of requirements by itself.
class SingleAllowMissingInOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_missing_yaml[] = R"(
requires_any:
  requirements:
  - allow_missing: {}
)";
    modifyRequirement(allow_missing_yaml);
    createVerifier();
  }
};

TEST_F(SingleAllowMissingInOrListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(SingleAllowMissingInOrListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(SingleAllowMissingInOrListTest, MissingIssToken) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ES256WithoutIssToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(SingleAllowMissingInOrListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(SingleAllowMissingInOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(SingleAllowMissingInOrListTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
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
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInOrListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowFailedInOrListTest, GoodAndBadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowFailedInOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  // Note: the first (provider) requirement is satisfied, so the allow_missing_or_failed has not
  // kicked in yet.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowFailedInOrListTest, BadAndGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken},
                                                {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
  // Token in x-other is not required, so it will be ignore.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
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
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInAndListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowFailedInAndListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{
      {kExampleHeader, GoodToken},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowFailedInAndListTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  // The bad, non-required token won't affect the verification status though.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowFailedInAndListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
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
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowFailedInAndOfOrListTest, BadJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, OtherGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, BadAndGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken},
                                                {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(AllowFailedInAndOfOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

// The `allow_missing` is defined in an OR-list of requirements.
class AllowMissingInOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_any:
  requirements:
  - provider_name: "example_provider"
  - allow_missing: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowMissingInOrListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowMissingInOrListTest, BadJwt) {
  // Bad JWT should fail.
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtVerificationFail));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowMissingInOrListTest, OtherGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // x-other JWT should be ignored.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowMissingInOrListTest, WrongIssuer) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // x-other JWT should be ignored.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowMissingInOrListTest, BadAndGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtVerificationFail));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken},
                                                {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
  // x-other JWT should be ignored.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

class AllowMissingInAndListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_all:
  requirements:
  - provider_name: "example_provider"
  - allow_missing: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowMissingInAndListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowMissingInAndListTest, BadJwt) {
  // Bad JWT should fail.
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtVerificationFail));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowMissingInAndListTest, GoodJwt) {
  // Bad JWT should fail.
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowMissingInAndListTest, TwoGoodJwts) {
  // Bad JWT should fail.
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

class AllowMissingInAndOfOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char allow_failed_yaml[] = R"(
requires_all:
  requirements:
  - requires_any:
      requirements:
      - provider_name: "example_provider"
      - allow_missing: {}
  - requires_any:
      requirements:
      - provider_name: "other_provider"
      - allow_missing: {}
)";
    modifyRequirement(allow_failed_yaml);
    createVerifier();
  }
};

TEST_F(AllowMissingInAndOfOrListTest, NoJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(AllowMissingInAndOfOrListTest, BadJwt) {
  // Bad JWT should fail.
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtVerificationFail));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(AllowMissingInAndOfOrListTest, OneGoodJwt) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(AllowMissingInAndOfOrListTest, TwoGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(AllowMissingInAndOfOrListTest, GoodAndBadJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));
  // Use the token with example.com issuer for x-other.
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(AllowMissingInAndOfOrListTest, BadAndGoodJwts) {
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken},
                                                {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
  // Short-circuit AND, the x-other JWT should be ignored.
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

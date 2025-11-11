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

// =============================================================================
// Extract-Only Tests
// =============================================================================

// Tests extract_only in a single requirement by itself
class ExtractOnlyInSingleRequirementTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    proto_config_.mutable_rules(0)->mutable_requires_()->mutable_extract_only();
    createVerifier();
  }
};

TEST_F(ExtractOnlyInSingleRequirementTest, NoJwt) {
  // Should succeed even with no JWT
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ExtractOnlyInSingleRequirementTest, BadJwt) {
  // Should succeed even with expired JWT
  // Note: allow_failed behavior - succeeds but may not extract payload on validation failure
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Payload extraction behavior matches allow_missing_or_failed
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyInSingleRequirementTest, MissingIssToken) {
  // Should succeed even with unknown issuer
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ES256WithoutIssToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Payload extraction behavior matches allow_missing_or_failed
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyInSingleRequirementTest, OneGoodJwt) {
  // Should succeed and extract claims from valid JWT
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(ExtractOnlyInSingleRequirementTest, TwoGoodJwts) {
  // Should extract claims from both JWTs
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(ExtractOnlyInSingleRequirementTest, GoodAndBadJwts) {
  // Should extract from good JWT and still succeed despite bad JWT
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  // Bad JWT - payload not extracted (matches allow_missing_or_failed behavior)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(ExtractOnlyInSingleRequirementTest, InvalidFormatJwt) {
  // Should succeed even with malformed JWT
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // May or may not extract depending on how malformed - key point is it succeeds
}

// Tests extract_only in an OR-list of requirements
class ExtractOnlyInOrListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char extract_only_yaml[] = R"(
requires_any:
  requirements:
  - provider_name: "example_provider"
  - extract_only: {}
)";
    modifyRequirement(extract_only_yaml);
    createVerifier();
  }
};

TEST_F(ExtractOnlyInOrListTest, NoJwt) {
  // extract_only allows missing, so should succeed
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ExtractOnlyInOrListTest, BadJwt) {
  // extract_only allows failed verification, so should succeed
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Payload not extracted from bad JWT (matches allow_missing_or_failed behavior)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyInOrListTest, OneGoodJwt) {
  // First requirement (example_provider) should succeed
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(ExtractOnlyInOrListTest, TwoGoodJwts) {
  // First requirement satisfied, other JWT ignored
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

TEST_F(ExtractOnlyInOrListTest, WrongIssuer) {
  // Provider requirement fails with wrong issuer, but extract_only fallback succeeds
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Wrong issuer - payload not extracted (matches allow_missing_or_failed behavior)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

// Tests extract_only in an AND-list of requirements
class ExtractOnlyInAndListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char extract_only_yaml[] = R"(
requires_all:
  requirements:
  - provider_name: "example_provider"
  - extract_only: {}
)";
    modifyRequirement(extract_only_yaml);
    createVerifier();
  }
};

TEST_F(ExtractOnlyInAndListTest, NoJwt) {
  // AND requires both: example_provider will fail with JwtMissed
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ExtractOnlyInAndListTest, BadJwt) {
  // example_provider will fail verification
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // extract_only would extract, but AND short-circuits on first failure
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyInAndListTest, OneGoodJwt) {
  // Both requirements satisfied (example_provider validates, extract_only extracts)
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(ExtractOnlyInAndListTest, TwoGoodJwts) {
  // Both providers' JWTs extracted
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

// Tests extract_only in complex nested requirements
class ExtractOnlyInNestedListTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
    const char extract_only_yaml[] = R"(
requires_all:
  requirements:
  - requires_any:
      requirements:
      - provider_name: "example_provider"
      - extract_only: {}
  - requires_any:
      requirements:
      - provider_name: "other_provider"
      - extract_only: {}
)";
    modifyRequirement(extract_only_yaml);
    createVerifier();
  }
};

TEST_F(ExtractOnlyInNestedListTest, NoJwt) {
  // Both OR-lists have extract_only fallback, so should succeed
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ExtractOnlyInNestedListTest, BadJwt) {
  // extract_only fallback in both OR-lists allows this to succeed
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Bad JWT - payload not extracted (matches allow_missing_or_failed behavior)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyInNestedListTest, OneGoodJwt) {
  // First OR satisfied by example_provider, second OR satisfied by extract_only
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
}

TEST_F(ExtractOnlyInNestedListTest, TwoGoodJwts) {
  // Both OR-lists satisfied by their respective providers
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers =
      Http::TestRequestHeaderMapImpl{{kExampleHeader, GoodToken}, {kOtherHeader, OtherGoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputSuccess(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputSuccess(kOtherHeader));
}

TEST_F(ExtractOnlyInNestedListTest, WrongIssuers) {
  // Both have wrong issuers but extract_only fallback allows success
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, OtherGoodToken},
                                                {kOtherHeader, GoodToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // Wrong issuers - payloads not extracted (matches allow_missing_or_failed behavior)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kOtherHeader));
}

// Test extract_only with allow_missing comparison
class ExtractOnlyVsAllowMissingTest : public AllVerifierTest {
protected:
  void SetUp() override {
    AllVerifierTest::SetUp();
  }
};

TEST_F(ExtractOnlyVsAllowMissingTest, ExtractOnlyWithBadJwt) {
  // extract_only should succeed with bad JWT (but may not extract payload)
  const char extract_only_yaml[] = R"(
requires_any:
  requirements:
  - provider_name: "example_provider"
  - extract_only: {}
)";
  modifyRequirement(extract_only_yaml);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, ExpiredToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  // extract_only succeeds but doesn't extract from bad JWT (same as allow_missing_or_failed)
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyVsAllowMissingTest, AllowMissingWithBadJwt) {
  // allow_missing should FAIL with bad JWT
  const char allow_missing_yaml[] = R"(
requires_any:
  requirements:
  - provider_name: "example_provider"
  - allow_missing: {}
)";
  modifyRequirement(allow_missing_yaml);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtVerificationFail));
  auto headers = Http::TestRequestHeaderMapImpl{{kExampleHeader, NonExistKidToken}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_THAT(headers, JwtOutputFailedOrIgnore(kExampleHeader));
}

TEST_F(ExtractOnlyVsAllowMissingTest, ExtractOnlyWithNoJwt) {
  // extract_only succeeds with no JWT
  const char extract_only_yaml[] = "extract_only: {}";
  modifyRequirement(extract_only_yaml);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ExtractOnlyVsAllowMissingTest, AllowMissingWithNoJwt) {
  // allow_missing also succeeds with no JWT
  const char allow_missing_yaml[] = "allow_missing: {}";
  modifyRequirement(allow_missing_yaml);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

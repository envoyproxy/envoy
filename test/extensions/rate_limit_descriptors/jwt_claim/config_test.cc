#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/extensions/rate_limit_descriptors/jwt_claim/v3/jwt_claim.pb.h"
#include "envoy/extensions/rate_limit_descriptors/jwt_claim/v3/jwt_claim.pb.validate.h"

#include "source/common/common/base64.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/router_ratelimit.h"
#include "source/extensions/rate_limit_descriptors/jwt_claim/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace JwtClaim {
namespace {

// Builds an unsigned JWT string ("header.payload.signature") from a literal
// JSON payload body, for use as test fixtures. The signature section is not
// cryptographically valid since this descriptor extension does not verify it.
std::string makeJwt(const std::string& payload_json,
                    const std::string& header_json = R"({"alg":"RS256"})") {
  return absl::StrCat(Base64Url::encode(header_json.data(), header_json.size()), ".",
                      Base64Url::encode(payload_json.data(), payload_json.size()), ".",
                      Base64Url::encode("sig", 3));
}

class JwtClaimDescriptorTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    envoy::config::route::v3::RateLimit rate_limit;
    TestUtility::loadFromYaml(yaml, rate_limit);
    TestUtility::validate(rate_limit);
    absl::Status creation_status;
    rate_limit_entry_ =
        std::make_unique<Router::RateLimitPolicyEntryImpl>(rate_limit, context_, creation_status);
    THROW_IF_NOT_OK_REF(creation_status);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<Router::RateLimitPolicyEntryImpl> rate_limit_entry_;
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

std::string yamlFor(const std::string& extra_fields = "") {
  return absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: sub
)EOF",
                      extra_fields);
}

TEST(JwtClaimDescriptorFactoryTest, ProvidesExtensionMetadata) {
  JwtClaimDescriptorFactory factory;

  EXPECT_EQ("envoy.rate_limit_descriptors.jwt_claim", factory.name());
  EXPECT_EQ("envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor",
            factory.createEmptyConfigProto()->GetTypeName());
}

TEST_F(JwtClaimDescriptorTest, ExtractsSimpleClaim) {
  setupTest(yamlFor());
  const std::string jwt = makeJwt(R"({"sub":"user-123"})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "user-123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, ExtractsEmptyStringClaim) {
  setupTest(yamlFor("      skip_if_absent: true\n"));
  const std::string jwt = makeJwt(R"({"sub":""})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", ""}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, ExtractsNestedClaim) {
  const std::string yaml = absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: nested.key
)EOF");
  setupTest(yaml);
  const std::string jwt = makeJwt(R"({"nested":{"key":"nested-value"}})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "nested-value"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, MissingHeaderNoDefaultAbortsDescriptor) {
  setupTest(yamlFor());
  Http::TestRequestHeaderMapImpl header{};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(JwtClaimDescriptorTest, MissingHeaderWithSkipIfAbsentSkipsDescriptor) {
  const std::string yaml = absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: missing_claim
      skip_if_absent: true
- extension:
    name: jwt_claim_descriptor2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: second_descriptor
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: sub
)EOF");
  setupTest(yaml);
  const std::string jwt = makeJwt(R"({"sub":"user-123"})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"second_descriptor", "user-123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, DefaultValueUsedWhenClaimAbsent) {
  const std::string yaml = absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: sub
      default_value: anonymous
)EOF");
  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "anonymous"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, MalformedJwtUsesDefaultValue) {
  const std::string yaml = absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      value_prefix: "Bearer "
      claim_name: sub
      default_value: anonymous
)EOF");
  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"authorization", "Bearer not-a-jwt"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "anonymous"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, NonStringClaimTreatedAsAbsent) {
  setupTest(yamlFor());
  const std::string jwt = makeJwt(R"({"sub":42})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(JwtClaimDescriptorTest, ValuePrefixMismatchTreatedAsAbsent) {
  setupTest(yamlFor());
  const std::string jwt = makeJwt(R"({"sub":"user-123"})");
  // Header does not start with the configured "Bearer " prefix.
  Http::TestRequestHeaderMapImpl header{{"authorization", jwt}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(JwtClaimDescriptorTest, NoValuePrefixConfigured) {
  const std::string yaml = absl::StrCat(R"EOF(
actions:
- extension:
    name: jwt_claim_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor
      descriptor_key: my_descriptor_name
      header_name: authorization
      claim_name: sub
)EOF");
  setupTest(yaml);
  const std::string jwt = makeJwt(R"({"sub":"user-123"})");
  Http::TestRequestHeaderMapImpl header{{"authorization", jwt}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "user-123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(JwtClaimDescriptorTest, ForgedJwtStillExtractsUnverifiedClaim) {
  // Demonstrates that this extension does not verify the JWT signature: an
  // arbitrary, unsigned token with a made-up claim value is still accepted.
  setupTest(yamlFor());
  const std::string jwt = makeJwt(R"({"sub":"attacker-controlled-value"})");
  Http::TestRequestHeaderMapImpl header{{"authorization", absl::StrCat("Bearer ", jwt)}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"my_descriptor_name", "attacker-controlled-value"}}}}),
              testing::ContainerEq(descriptors_));
}

} // namespace
} // namespace JwtClaim
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy

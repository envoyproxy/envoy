#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/extensions/rate_limit_descriptors/expr/v3/expr.pb.h"
#include "envoy/extensions/rate_limit_descriptors/expr/v3/expr.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/router/router_ratelimit.h"
#include "source/extensions/rate_limit_descriptors/expr/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace Expr {
namespace {

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    envoy::config::route::v3::RateLimit rate_limit;
    TestUtility::loadFromYaml(yaml, rate_limit);
    TestUtility::validate(rate_limit);
    absl::Status creation_status;
    rate_limit_entry_ =
        std::make_unique<Router::RateLimitPolicyEntryImpl>(rate_limit, context_, creation_status);
    THROW_IF_NOT_OK(creation_status);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<Router::RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestRequestHeaderMapImpl header_;
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RateLimitPolicyEntryTest, MissingExtension) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Value
  )EOF";

  EXPECT_THROW_WITH_REGEX(setupTest(yaml), EnvoyException, ".*'custom'.*");
}

TEST_F(RateLimitPolicyEntryTest, ExpressionUnset) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
  )EOF";

  EXPECT_THROW_WITH_REGEX(setupTest(yaml), EnvoyException,
                          "Rate limit descriptor extension failed: .*");
}

#if defined(USE_CEL_PARSER)
TEST_F(RateLimitPolicyEntryTest, ExpressionText) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: request.headers["x-header-name"]
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, FormatConversionV1AlphaToDevCel) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: request.headers[":method"] == "GET"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":method", "GET"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "true"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, ExpressionWithDifferentDataTypes) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: string_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: string_value
      text: request.headers[":method"]
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":method", "GET"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);

  EXPECT_EQ(1, descriptors_.size());
  // Check the descriptor has the correct value
  EXPECT_EQ("GET", descriptors_[0].entries_[0].value_);
}

// Test boolean expression evaluation
TEST_F(RateLimitPolicyEntryTest, BooleanExpressionEvaluation) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: boolean_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: boolean_value
      text: request.headers[":method"] == "GET"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":method", "GET"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);

  EXPECT_EQ(1, descriptors_.size());
  // Check the descriptor has the correct value - boolean results are converted to strings
  EXPECT_EQ("true", descriptors_[0].entries_[0].value_);
}

// Test numeric expression evaluation
TEST_F(RateLimitPolicyEntryTest, NumericExpressionEvaluation) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: number_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: number_value
      text: size(request.headers[":method"])
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":method", "GET"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);

  EXPECT_EQ(1, descriptors_.size());
  // The numeric result is converted to a string
  EXPECT_EQ("3", descriptors_[0].entries_[0].value_);
}

TEST_F(RateLimitPolicyEntryTest, ExpressionTextMalformed) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: undefined_ext(false)
  )EOF";

  EXPECT_THROW_WITH_REGEX(setupTest(yaml), EnvoyException, "failed to create an expression: .*");
}

TEST_F(RateLimitPolicyEntryTest, ExpressionUnparsable) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: ++
  )EOF";

  EXPECT_THROW_WITH_REGEX(setupTest(yaml), EnvoyException,
                          "Unable to parse descriptor expression: .*");
}

TEST_F(RateLimitPolicyEntryTest, ComplexExpressionWithConditionals) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: complex_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: processed_path
      text: "request.headers[\":path\"] == \"/api/users\" ? \"api_path\" : \"no_path\""
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/api/users"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_EQ(1, descriptors_.size());

  // Test with a different path
  descriptors_.clear();
  Http::TestRequestHeaderMapImpl header2{{":path", "/other/path"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header2, stream_info_);
  EXPECT_EQ(1, descriptors_.size());
}

TEST_F(RateLimitPolicyEntryTest, ExpressionTextError) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: first_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: test_key
      text: "'a'"
- extension:
    name: second_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: request.headers["x-header-name"]
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, ExpressionTextErrorSkip) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: first_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: test_key
      text: "'a'"
- extension:
    name: second_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      text: request.headers["x-header-name"]
      skip_if_error: true
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"test_key", "a"}}}}),
              testing::ContainerEq(descriptors_));
}
#endif

TEST_F(RateLimitPolicyEntryTest, ExpressionParsed) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      parsed:
        call_expr:
          function: _==_
          args:
          - select_expr:
              operand:
                ident_expr:
                  name: request
              field: method
          - const_expr:
              string_value: GET
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":method", "GET"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_descriptor_name", "true"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, ExpressionParsedMalformed) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: my_descriptor_name
      parsed:
        call_expr:
          function: undefined_extent
          args:
          - const_expr:
              bool_value: false
  )EOF";

  EXPECT_THROW_WITH_REGEX(setupTest(yaml), EnvoyException, "failed to create an expression: .*");
}

TEST_F(RateLimitPolicyEntryTest, ExprSpecifierNotSet) {
  const std::string yaml = R"EOF(
actions:
- extension:
    name: custom_descriptor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
      descriptor_key: test_key
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      setupTest(yaml), EnvoyException,
      "Rate limit descriptor extension failed: expression specifier is not set");
}

} // namespace
} // namespace Expr
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy

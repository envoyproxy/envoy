#include "envoy/extensions/http/unified_header_validators/envoy_default/v3/unified_header_validator.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/http/unified_header_validators/envoy_default/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::UnifiedHeaderValidatorFactory;

namespace {

constexpr absl::string_view empty_config = R"EOF(
    name: envoy.http.unified_header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.unified_header_validators.envoy_default.v3.UnifiedHeaderValidatorConfig
)EOF";

} // namespace

class UnifiedHeaderValidatorTest : public testing::Test {
protected:
  UnifiedHeaderValidatorTest() = default;

  ::Envoy::Http::UnifiedHeaderValidatorPtr
  create(absl::string_view config_yaml, UnifiedHeaderValidatorFactory::Protocol protocol) {
    auto* factory =
        Registry::FactoryRegistry<Envoy::Http::UnifiedHeaderValidatorFactoryConfig>::getFactory(
            "envoy.http.unified_header_validators.envoy_default");
    ASSERT(factory != nullptr);

    envoy::config::core::v3::TypedExtensionConfig typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    uhv_factory_ = factory->createFromProto(typed_config.typed_config(), context_);
    return uhv_factory_->create(protocol);
  }

  void setHeaderStringUnvalidated(HeaderString& header_string, absl::string_view value) {
    header_string.setCopyUnvalidatedForTestOnly(value);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ::Envoy::Http::UnifiedHeaderValidatorFactorySharedPtr uhv_factory_;
};

TEST_F(UnifiedHeaderValidatorTest, Http1HeaderNameValidation) {
  auto uhv = create(empty_config, UnifiedHeaderValidatorFactory::Protocol::HTTP1);
  // Since the default UHV does not yet check anything all header values should be accepted
  std::string key_value("aaa");
  HeaderString key(key_value);
  HeaderString value("valid");
  for (int c = 0; c <= 0xff; ++c) {
    key_value[1] = c;
    setHeaderStringUnvalidated(key, key_value);
    EXPECT_EQ(uhv->validateHeaderEntry(key, value),
              ::Envoy::Http::UnifiedHeaderValidator::HeaderEntryValidationResult::Accept);
  }
}

TEST_F(UnifiedHeaderValidatorTest, Http1RequestHeaderMapValidation) {
  auto uhv = create(empty_config, UnifiedHeaderValidatorFactory::Protocol::HTTP1);
  ::Envoy::Http::TestRequestHeaderMapImpl request_header_map{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  EXPECT_EQ(uhv->validateRequestHeaderMap(request_header_map),
            ::Envoy::Http::UnifiedHeaderValidator::RequestHeaderMapValidationResult::Accept);
}

TEST_F(UnifiedHeaderValidatorTest, Http1ResponseHeaderMapValidation) {
  auto uhv = create(empty_config, UnifiedHeaderValidatorFactory::Protocol::HTTP1);
  ::Envoy::Http::TestResponseHeaderMapImpl response_header_map{{":status", "200"}};
  EXPECT_EQ(uhv->validateResponseHeaderMap(response_header_map),
            ::Envoy::Http::UnifiedHeaderValidator::ResponseHeaderMapValidationResult::Accept);
}

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy

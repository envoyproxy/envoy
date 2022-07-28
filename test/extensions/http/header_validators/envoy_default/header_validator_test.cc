#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/http/header_validators/envoy_default/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::HeaderValidatorFactory;

namespace {

constexpr absl::string_view empty_config = R"EOF(
    name: envoy.http.header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig
)EOF";

} // namespace

class HeaderValidatorTest : public testing::Test {
protected:
  ::Envoy::Http::HeaderValidatorPtr create(absl::string_view config_yaml,
                                           ::Envoy::Http::Protocol protocol) {
    auto* factory =
        Registry::FactoryRegistry<Envoy::Http::HeaderValidatorFactoryConfig>::getFactory(
            "envoy.http.header_validators.envoy_default");
    ASSERT(factory != nullptr);

    envoy::config::core::v3::TypedExtensionConfig typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    uhv_factory_ = factory->createFromProto(typed_config.typed_config(), context_);
    return uhv_factory_->create(protocol, stream_info_);
  }

  void setHeaderStringUnvalidated(HeaderString& header_string, absl::string_view value) {
    header_string.setCopyUnvalidatedForTestOnly(value);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ::Envoy::Http::HeaderValidatorFactoryPtr uhv_factory_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(HeaderValidatorTest, Http1RequestHeaderNameValidation) {
  auto uhv = create(empty_config, ::Envoy::Http::Protocol::Http11);
  // Since the default UHV does not yet check anything all header values should be accepted
  std::string key_value("aaa");
  HeaderString key(key_value);
  HeaderString value("valid");
  for (int c = 0; c <= 0xff; ++c) {
    key_value[1] = c;
    setHeaderStringUnvalidated(key, key_value);
    EXPECT_TRUE(uhv->validateRequestHeaderEntry(key, value));
  }
}

TEST_F(HeaderValidatorTest, Http1ResponseHeaderNameValidation) {
  auto uhv = create(empty_config, ::Envoy::Http::Protocol::Http11);
  // Since the default UHV does not yet check anything all header values should be accepted
  std::string key_value("aaa");
  HeaderString key(key_value);
  HeaderString value("valid");
  for (int c = 0; c <= 0xff; ++c) {
    key_value[1] = c;
    setHeaderStringUnvalidated(key, key_value);
    EXPECT_TRUE(uhv->validateResponseHeaderEntry(key, value).ok());
  }
}

TEST_F(HeaderValidatorTest, Http1RequestHeaderMapValidation) {
  auto uhv = create(empty_config, ::Envoy::Http::Protocol::Http11);
  ::Envoy::Http::TestRequestHeaderMapImpl request_header_map{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  EXPECT_TRUE(uhv->validateRequestHeaderMap(request_header_map));
}

TEST_F(HeaderValidatorTest, Http1ResponseHeaderMapValidation) {
  auto uhv = create(empty_config, ::Envoy::Http::Protocol::Http11);
  ::Envoy::Http::TestResponseHeaderMapImpl response_header_map{{":status", "200"}};
  EXPECT_TRUE(uhv->validateResponseHeaderMap(response_header_map).ok());
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy

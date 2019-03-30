#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::envoy::api::v2::core::Metadata;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

TEST(HttpJwtAuthnFilterConfigTest, FindByMatch) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
  requires:
    provider_name: provider1
)";

  JwtAuthentication proto_config;
  MessageUtil::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterConfig filter_conf(proto_config, "", context);

  envoy::api::v2::core::Metadata metadata;
  EXPECT_TRUE(filter_conf.findVerifier(
                  Http::TestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path1"},
                  },
                  metadata) != nullptr);

  EXPECT_TRUE(filter_conf.findVerifier(
                  Http::TestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path2"},
                  },
                  metadata) == nullptr);
}

void setStringMetadata(Metadata& metadata, const std::string& filter, const std::string& key,
                       const std::string& value) {
  ProtobufWkt::Value proto_value;
  proto_value.set_string_value(value);
  ProtobufWkt::Struct md;
  (*md.mutable_fields())[key] = proto_value;
  (*metadata.mutable_filter_metadata())[filter].MergeFrom(md);
}

TEST(HttpJwtAuthnFilterConfigTest, FindByMetadata) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
  provider2:
    issuer: issuer2
    local_jwks:
      inline_string: jwks
metadata_rules:
  filter: selector_filter
  path:
  - selector
  requires:
    selector1:
      provider_name: provider1
    selector2:
      provider_name: provider2
)";

  JwtAuthentication proto_config;
  MessageUtil::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterConfig filter_conf(proto_config, "", context);

  envoy::api::v2::core::Metadata metadata;
  // Empty metadata
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), metadata) == nullptr);

  // Wrong selector
  setStringMetadata(metadata, "selector_filter", "selector", "wrong_selector");
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), metadata) == nullptr);

  // correct selector
  setStringMetadata(metadata, "selector_filter", "selector", "selector1");
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), metadata) != nullptr);

  // correct selector
  setStringMetadata(metadata, "selector_filter", "selector", "selector2");
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), metadata) != nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

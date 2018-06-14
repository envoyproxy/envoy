#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.validate.h"

#include "extensions/filters/http/jwt_authn/filter_factory.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using testing::Invoke;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

TEST(HttpJwtAuthnFilterFactoryTest, GoodRemoteJwks) {
  FilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  MessageUtil::loadFromYaml(ExampleConfig, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpJwtAuthnFilterFactoryTest, InvalidIssuerProto) {
  FilterFactory factory;
  JwtAuthentication proto_config;
  // Add an empty rule with empty issuer
  proto_config.add_rules();

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context),
               ProtoValidationException);
}

TEST(HttpJwtAuthnFilterFactoryTest, GoodLocalJwks) {
  JwtAuthentication proto_config;
  auto rule = proto_config.add_rules();
  rule->set_issuer("issuer");
  rule->mutable_local_jwks()->set_inline_string(PublicKey);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpJwtAuthnFilterFactoryTest, BadLocalJwks) {
  JwtAuthentication proto_config;
  auto rule = proto_config.add_rules();
  rule->set_issuer("issuer");
  rule->mutable_local_jwks()->set_inline_string("A bad jwks");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactory factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context),
               EnvoyException);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

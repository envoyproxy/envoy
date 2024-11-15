#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.validate.h"

#include "source/extensions/filters/http/jwt_authn/filter_config.h"
#include "source/extensions/filters/http/jwt_authn/filter_factory.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

TEST(HttpJwtAuthnFilterFactoryTest, GoodRemoteJwks) {
  FilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(ExampleConfig, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpJwtAuthnFilterFactoryTest, GoodLocalJwks) {
  JwtAuthentication proto_config;
  auto& provider = (*proto_config.mutable_providers())["provider"];
  provider.set_issuer("issuer");
  provider.mutable_local_jwks()->set_inline_string(PublicKey);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpJwtAuthnFilterFactoryTest, BadLocalJwks) {
  JwtAuthentication proto_config;
  auto& provider = (*proto_config.mutable_providers())["provider"];
  provider.set_issuer("issuer");
  provider.mutable_local_jwks()->set_inline_string("A bad jwks");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactory factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).value(),
               EnvoyException);
}

TEST(HttpJwtAuthnFilterFactoryTest, ProviderWithoutIssuer) {
  JwtAuthentication proto_config;
  auto& provider = (*proto_config.mutable_providers())["provider"];
  // This provider did not specify "issuer".
  provider.mutable_local_jwks()->set_inline_string(PublicKey);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(HttpJwtAuthnFilterFactoryTest, EmptyPerRouteConfig) {
  PerRouteConfig per_route;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FilterFactory factory;
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(per_route, context,
                                                       context.messageValidationVisitor()),
               EnvoyException);
}

TEST(HttpJwtAuthnFilterFactoryTest, WrongPerRouteConfigType) {
  JwtAuthentication per_route;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FilterFactory factory;
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(per_route, context,
                                                       context.messageValidationVisitor()),
               std::bad_cast);
}

TEST(HttpJwtAuthnFilterFactoryTest, DisabledPerRouteConfig) {
  PerRouteConfig per_route;
  per_route.set_disabled(true);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FilterFactory factory;
  auto base_ptr = factory.createRouteSpecificFilterConfig(per_route, context,
                                                          context.messageValidationVisitor());
  EXPECT_NE(base_ptr, nullptr);
  const PerRouteFilterConfig* typed_ptr = dynamic_cast<const PerRouteFilterConfig*>(base_ptr.get());
  EXPECT_NE(typed_ptr, nullptr);
  EXPECT_TRUE(typed_ptr->config().disabled());
}

TEST(HttpJwtAuthnFilterFactoryTest, GoodPerRouteConfig) {
  PerRouteConfig per_route;
  per_route.set_requirement_name("name");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FilterFactory factory;
  auto base_ptr = factory.createRouteSpecificFilterConfig(per_route, context,
                                                          context.messageValidationVisitor());
  EXPECT_NE(base_ptr, nullptr);
  const PerRouteFilterConfig* typed_ptr = dynamic_cast<const PerRouteFilterConfig*>(base_ptr.get());
  EXPECT_NE(typed_ptr, nullptr);
  EXPECT_EQ(typed_ptr->config().requirement_name(), "name");
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

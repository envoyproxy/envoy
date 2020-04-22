#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.validate.h"

#include "extensions/filters/http/jwt_authn/filter_factory.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
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

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
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
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
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
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context),
               EnvoyException);
}
} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
